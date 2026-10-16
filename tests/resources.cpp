#include <iiSDKBuilder/Resources.h>
#include <iiSDKCore/Resource/Pack.h>
#include <iiFileProviderNative.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ii;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool value,const char* what) { ++checks;if (!value) throw std::runtime_error(what); }
template<class T> void good(const T& value,const char* what) {
    if (!value) throw std::runtime_error(std::string(what)+": "+value.error().message);++checks;
}
std::string read(const fs::path& path) { return iiFileProvider::native::File::read(path); }
void write(const fs::path& path,std::string_view text) {
    iiFileProvider::native::File::createDirectories(path.parent_path());iiFileProvider::native::File::write(path,text);
}
std::string text(const resource::View& view) {
    return {reinterpret_cast<const char*>(view.bytes().data()),view.bytes().size()};
}
void run(const fs::path& directory) {
    const auto input=directory/"input files";const auto manifest=input/"resources.json";
    write(input/"common 한글.txt","common payload");
    write(input/"native.txt","native payload");
    const auto manifest_text=R"({"schema":1,"name":"demo","resources":[
        {"name":"greeting","source":"common 한글.txt"},
        {"name":"alias","source":"common 한글.txt"},
        {"name":"greeting","source":"native.txt","platform":"macos"},
        {"name":"foreign-only","source":"does-not-exist","platform":"android"}
    ]})";
    write(manifest,manifest_text);
    const auto output=directory/"generated files";
    builder::ResourceOutputs outputs{output/"demo.cpp",output/"demo.h",output/"demo.iirp",output/"demo.d"};
    auto built=builder::compile_resources(manifest,builder::Platform::macos,outputs);good(built,"compile resources");
    check(built->entries==2 && built->unique_payloads==2 && built->changed,"target override and foreign stripping");
    const auto raw=read(outputs.pack);auto pack=resource::Pack::copy(core::byte_view(raw));good(pack,"read generated native pack");
    check(text(*pack->find("greeting"))=="native payload" && text(*pack->find("alias"))=="common payload","target bytes");
    check(!pack->find("foreign-only"),"unselected missing file never opened/packed");
    check(read(outputs.cpp).find("alignas(ii::resource::PackAlignment)")!=std::string::npos &&
        read(outputs.header).find("load_demo")!=std::string::npos,"native constant C++ generation");
    check(read(outputs.depfile).find("input\\ files")!=std::string::npos &&
        read(outputs.depfile).find("common\\ 한글.txt")!=std::string::npos &&
        read(outputs.depfile).find("does-not-exist")==std::string::npos,"escaped target dependencies only");
    const auto stamp=fs::last_write_time(outputs.cpp);
    auto again=builder::compile_resources(manifest,builder::Platform::macos,outputs);good(again,"repeat deterministic compile");
    check(!again->changed && stamp==fs::last_write_time(outputs.cpp),"unchanged content preserves timestamps");
    auto windows=builder::compile_resources(manifest,builder::Platform::windows,{.pack=output/"windows.iirp"});good(windows,"target-independent pack generation");
    const auto win_raw=read(output/"windows.iirp");auto win_pack=resource::Pack::copy(core::byte_view(win_raw));good(win_pack,"Windows data pack");
    check(windows->unique_payloads==1 && text(*win_pack->find("greeting"))=="common payload","common fallback and deduplication");
    check(!builder::compile_resources(manifest,builder::Platform::macos,{.pack=manifest}) &&
        read(manifest)==manifest_text,"input manifest cannot be overwritten");
    check(!builder::compile_resources(manifest,builder::Platform::macos,{.pack=input/"native.txt"}) &&
        read(input/"native.txt")=="native payload","source asset cannot be overwritten");
    const auto hardlink=output/"source-alias.iirp";
    if (fs::exists(hardlink)) (void)iiFileProvider::native::File::remove(hardlink);
    fs::create_hard_link(input/"native.txt",hardlink); // Test-only native identity fixture.
    check(!builder::compile_resources(manifest,builder::Platform::macos,{.pack=hardlink}) &&
        read(input/"native.txt")=="native payload","native file identity aliases cannot replace a source");
    (void)iiFileProvider::native::File::remove(hardlink);
    fs::create_hard_link(outputs.cpp,hardlink);
    check(!builder::compile_resources(manifest,builder::Platform::macos,
        {.cpp=outputs.cpp,.header=hardlink}),"native identity aliases between outputs are rejected");
    (void)iiFileProvider::native::File::remove(hardlink);
    check(!builder::compile_resources(manifest,builder::Platform::macos,{.cpp=outputs.cpp}),"incomplete C++ pair rejected");
    check(!builder::compile_resources(manifest,builder::Platform::macos,{.pack=outputs.cpp,.depfile=outputs.cpp}),"output collision rejected");
    check(!builder::compile_resources(manifest,static_cast<builder::Platform>(99),outputs),"unknown target rejected");
    const auto original=read(outputs.cpp);
    for (const auto* invalid:{
        R"({"schema":2,"name":"demo","resources":[]})",
        R"({"schema":1,"name":"bad-name","resources":[]})",
        R"({"schema":1,"name":"demo","resources":[{"name":"x","source":"native.txt"},{"name":"x","source":"native.txt"}]})",
        R"({"schema":1,"name":"demo","resources":[{"name":"../x","source":"native.txt"}]})",
        R"({"schema":1,"name":"demo","resources":[{"name":"x","source":"native.txt","platform":"typo"}]})",
        R"({"schema":1,"name":"demo","resources":[{"name":"x","source":"missing"}]})",
        R"({"schema":1,"name":"demo","maximumBytes":1,"resources":[{"name":"x","source":"native.txt"}]})"
    }) {
        write(manifest,invalid);check(!builder::compile_resources(manifest,builder::Platform::macos,outputs),"invalid manifest fails");
        check(read(outputs.cpp)==original,"failure preserves previous artifact");
    }
    write(manifest,manifest_text);
    write(input/"native.txt","changed native payload");
    auto updated=builder::compile_resources(manifest,builder::Platform::macos,outputs);good(updated,"changed asset recompiles");
    check(updated->changed && read(outputs.cpp)!=original,"new payload changes generated native bytes");
}
}
int main(int argc,char** argv) {
    try { if (argc!=2) throw std::runtime_error("missing output directory");
        run(fs::absolute(argv[1]));std::cout<<"Resource compiler: "<<checks<<" checks passed\n";return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
