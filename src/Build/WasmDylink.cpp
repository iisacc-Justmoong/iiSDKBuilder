#include "WasmDylink.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ii::builder::detail {
namespace {
[[noreturn]] void bad(const char* message) {throw std::runtime_error(message);}
struct Cursor final {
    std::ifstream& input;
    std::uint64_t pos,limit;
    unsigned char byte() {
        if(pos>=limit)bad("Truncated WebAssembly metadata");
        input.clear();input.seekg(static_cast<std::streamoff>(pos++));char c=0;
        if(!input.get(c))bad("Cannot read WebAssembly metadata");return static_cast<unsigned char>(c);
    }
    std::uint32_t leb() {
        std::uint32_t out=0;
        for(unsigned i=0;i<5;++i) {
            const auto b=byte();
            if(i==4 && (b&0xf0))bad("WebAssembly integer overflow");
            out|=std::uint32_t(b&0x7f)<<(7*i);
            if(!(b&0x80))return out;
        }
        bad("Invalid WebAssembly integer");
    }
    std::string string(std::size_t maximum=4096) {
        const auto count=leb();if(count>maximum || count>limit-pos)bad("Invalid WebAssembly string length");
        std::string out(count,'\0');input.clear();input.seekg(static_cast<std::streamoff>(pos));
        if(count && !input.read(out.data(),static_cast<std::streamsize>(count)))bad("Cannot read WebAssembly string");
        pos+=count;return out;
    }
    Cursor subsection(std::uint64_t length) {
        if(length>limit-pos)bad("Invalid WebAssembly section length");
        Cursor child{input,pos,pos+length};pos+=length;return child;
    }
};
void names(Cursor& cursor,std::vector<std::string>& result) {
    const auto count=cursor.leb();if(count>4096)bad("Too many WebAssembly side modules");
    for(std::uint32_t i=0;i<count;++i) {
        auto name=cursor.string(1024);
        if(name.empty() || name.front()=='/' || name.find_first_of("\\:\r\n;")!=std::string::npos || name.find('\0')!=std::string::npos)
            bad("Unsafe WebAssembly side module name");
        std::string_view rest=name;
        while(true) {
            const auto split=rest.find('/');const auto part=rest.substr(0,split);
            if(part.empty()||part=="."||part=="..")bad("Unsafe WebAssembly side module path");
            if(split==std::string_view::npos)break;rest.remove_prefix(split+1);
        }
        if(std::ranges::find(result,name)==result.end())result.push_back(std::move(name));
    }
}
} // namespace
core::Result<std::vector<std::string>> wasm_needed(const std::filesystem::path& file) try {
    std::ifstream input(file,std::ios::binary);if(!input)bad("Cannot open WebAssembly module");
    input.seekg(0,std::ios::end);const auto end=input.tellg();
    if(end<8 || static_cast<std::uint64_t>(end)>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        bad("Invalid WebAssembly module length");
    Cursor top{input,0,static_cast<std::uint64_t>(end)};
    constexpr unsigned char header[]{0,'a','s','m',1,0,0,0};
    for(auto expected:header)if(top.byte()!=expected)bad("Invalid WebAssembly header");
    std::vector<std::string> result;bool found=false;
    while(top.pos<top.limit) {
        const auto type=top.byte();const auto length=top.leb();auto section=top.subsection(length);
        if(type!=0)continue;
        const auto name=section.string();if(name!="dylink.0" && name!="dylink")continue;
        if(found)bad("Multiple WebAssembly dylink sections");found=true;
        if(name=="dylink") {
            (void)section.leb();(void)section.leb();(void)section.leb();(void)section.leb();names(section,result);
        } else {
            bool needed=false;
            while(section.pos<section.limit) {
                const auto sub_type=section.leb();const auto sub_length=section.leb();auto sub=section.subsection(sub_length);
                if(sub_type==2) {
                    if(needed)bad("Duplicate WebAssembly needed subsection");needed=true;names(sub,result);
                    if(sub.pos!=sub.limit)bad("Trailing WebAssembly needed metadata");
                }
            }
        }
    }
    return result;
} catch(const std::exception& error) {return core::fail(core::ErrorCode::invalid_argument,error.what());}
} // namespace ii::builder::detail
