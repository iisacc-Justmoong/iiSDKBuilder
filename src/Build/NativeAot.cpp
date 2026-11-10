#include <iiSDKBuilder/NativeAot.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ii::builder {
namespace {
namespace fs=std::filesystem;
bool identifier_start(unsigned char c){return std::isalpha(c)||c=='_';}
bool identifier_part(unsigned char c){return std::isalnum(c)||c=='_';}

// Remove comments and literals before inspecting tokens: source text in a
// diagnostic, comment or raw string must not register a nonexistent plugin.
bool plugin_definition(const std::string& source){
    std::vector<std::string_view> tokens;
    for(std::size_t i=0;i<source.size();){
        const auto c=static_cast<unsigned char>(source[i]);
        if(std::isspace(c)){++i;continue;}
        if(source.compare(i,2,"//")==0){const auto end=source.find('\n',i+2);i=end==std::string::npos?source.size():end;continue;}
        if(source.compare(i,2,"/*")==0){const auto end=source.find("*/",i+2);if(end==std::string::npos)throw std::runtime_error("Unterminated source comment");i=end+2;continue;}
        std::size_t raw=i;
        if(source.compare(i,3,"u8R")==0)raw=i+2;
        else if((source[i]=='u'||source[i]=='U'||source[i]=='L')&&source.compare(i+1,2,"R\"")==0)raw=i+1;
        if(source.compare(raw,2,"R\"")==0){
            const auto open=source.find('(',raw+2);
            if(open==std::string::npos||open-raw>18)throw std::runtime_error("Invalid raw string delimiter");
            const auto delimiter=source.substr(raw+2,open-(raw+2));
            const auto end=source.find(")"+delimiter+"\"",open+1);
            if(end==std::string::npos)throw std::runtime_error("Unterminated raw string");
            i=end+delimiter.size()+2;continue;
        }
        if(std::isdigit(c)){
            const auto begin=i++;
            while(i<source.size()){
                const auto part=static_cast<unsigned char>(source[i]);
                if(!std::isalnum(part)&&part!='_'&&part!='\''&&part!='.')break;
                ++i;
            }
            tokens.emplace_back(source.data()+begin,i-begin);continue;
        }
        if(c=='"'||c=='\''){
            const auto quote=source[i++];
            bool closed=false;
            while(i<source.size()){
                if(source[i]=='\\'&&i+1<source.size()){i+=2;continue;}
                if(source[i++]==quote){closed=true;break;}
            }
            if(!closed)throw std::runtime_error("Unterminated source literal");
            continue;
        }
        if(identifier_start(c)){
            const auto begin=i++;
            while(i<source.size()&&identifier_part(static_cast<unsigned char>(source[i])))++i;
            tokens.emplace_back(source.data()+begin,i-begin);continue;
        }
        tokens.emplace_back(source.data()+i,1);++i;
    }
    for(std::size_t i=0;i+1<tokens.size();++i){
        if(tokens[i]!="ii_sdk_plugin_v1"||tokens[i+1]!="(")continue;
        std::size_t j=i+1,depth=0;
        do {if(tokens[j]=="(")++depth;else if(tokens[j]==")")--depth;++j;}while(j<tokens.size()&&depth);
        if(depth)continue;
        // A function call in an if/while condition is followed by ')' and
        // later by a block too. Only accept the plugin's definition syntax,
        // not any later opening brace in the surrounding statement.
        if(j<tokens.size()&&tokens[j]=="noexcept") {
            ++j;
            if(j<tokens.size()&&tokens[j]=="(") {
                std::size_t nested=0;
                do {if(tokens[j]=="(")++nested;else if(tokens[j]==")")--nested;++j;}while(j<tokens.size()&&nested);
                if(nested)continue;
            }
        }
        if(j<tokens.size()&&tokens[j]=="{")return true;
    }
    return false;
}
bool source_file(const fs::path& path){
    auto ext=path.extension().string();
    std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return ext==".cpp"||ext==".cc"||ext==".cxx"||ext==".mm";
}
bool ignored_directory(const fs::path& path){
    const auto name=path.filename().string();
    static const std::set<std::string> ignored{"build","tests","test","docs","example","examples","node_modules"};
    return name.empty()||name.front()=='.'||ignored.contains(name);
}
}
core::Result<std::vector<fs::path>> discover_static_plugins(const fs::path& root){
    try{
        if(!fs::is_directory(root))return core::fail(core::ErrorCode::not_found,"Static plugin source root is not a directory: "+root.string());
        std::vector<fs::path> result;
        const auto absolute=fs::weakly_canonical(fs::absolute(root));
        std::size_t files=0;
        for(fs::recursive_directory_iterator it(absolute),end;it!=end;++it){
            const auto& entry=*it;
            if(entry.is_symlink())return core::fail(core::ErrorCode::unsupported,"Symbolic links cannot be closed-world plugin sources: "+entry.path().string());
            if(entry.is_directory()){if(ignored_directory(entry.path()))it.disable_recursion_pending();continue;}
            if(!entry.is_regular_file()||!source_file(entry.path()))continue;
            if(++files>100000)return core::fail(core::ErrorCode::out_of_range,"Too many source files during static plugin discovery");
            if(entry.file_size()>8*1024*1024)return core::fail(core::ErrorCode::out_of_range,"Plugin source candidate exceeds 8 MiB: "+entry.path().string());
            std::ifstream in(entry.path(),std::ios::binary);
            if(!in)return core::fail(core::ErrorCode::failed,"Cannot read plugin source candidate: "+entry.path().string());
            const std::string source{std::istreambuf_iterator<char>(in),{}};
            if(in.bad())return core::fail(core::ErrorCode::failed,"Cannot finish reading plugin source candidate: "+entry.path().string());
            if(plugin_definition(source))result.push_back(entry.path());
        }
        std::ranges::sort(result);return result;
    }catch(...){return core::fail(core::ErrorCode::failed,"Cannot discover static plugin sources",std::current_exception());}
}
core::Result<std::vector<fs::path>> discover_static_python(const fs::path& root){
    try{
        if(!fs::is_directory(root))return core::fail(core::ErrorCode::not_found,"Python source root is not a directory: "+root.string());
        std::vector<fs::path> result;
        const auto absolute=fs::weakly_canonical(fs::absolute(root));
        for(fs::recursive_directory_iterator it(absolute),end;it!=end;++it){
            const auto& entry=*it;
            if(entry.is_symlink())return core::fail(core::ErrorCode::unsupported,"Symbolic links cannot be closed-world Python sources: "+entry.path().string());
            if(entry.is_directory()){if(ignored_directory(entry.path()))it.disable_recursion_pending();continue;}
            if(!entry.is_regular_file())continue;
            auto extension=entry.path().extension().string();
            std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extension!=".py")continue;
            if(result.size()>=10000||entry.file_size()>8*1024*1024)
                return core::fail(core::ErrorCode::out_of_range,"Python source discovery limit exceeded: "+entry.path().string());
            result.push_back(entry.path());
        }
        std::ranges::sort(result);return result;
    }catch(...){return core::fail(core::ErrorCode::failed,"Cannot discover native Python sources",std::current_exception());}
}
core::Result<std::vector<fs::path>> discover_uncompiled_runtime(const fs::path& root){
    try{
        if(!fs::is_directory(root))return core::fail(core::ErrorCode::not_found,"Runtime source root is not a directory: "+root.string());
        static const std::set<std::string> extensions{".pyc",".pyo",".js",".mjs",".lua",".luac",".wasm",".qml",".class",".jar",".dll",".dylib",".so",".spv"};
        std::vector<fs::path> result;
        const auto absolute=fs::weakly_canonical(fs::absolute(root));
        for(fs::recursive_directory_iterator it(absolute),end;it!=end;++it){
            const auto& entry=*it;
            if(entry.is_symlink())return core::fail(core::ErrorCode::unsupported,"Symbolic links cannot be closed-world runtime sources: "+entry.path().string());
            if(entry.is_directory()){if(ignored_directory(entry.path()))it.disable_recursion_pending();continue;}
            if(!entry.is_regular_file())continue;
            auto extension=entry.path().extension().string();
            std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            auto filename=entry.path().filename().string();
            std::ranges::transform(filename,filename.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extensions.contains(extension)||filename.find(".so.")!=std::string::npos)result.push_back(entry.path());
            if(result.size()>10000)return core::fail(core::ErrorCode::out_of_range,"Too many unsupported runtime sources");
        }
        std::ranges::sort(result);return result;
    }catch(...){return core::fail(core::ErrorCode::failed,"Cannot inspect unsupported runtime sources",std::current_exception());}
}
}
