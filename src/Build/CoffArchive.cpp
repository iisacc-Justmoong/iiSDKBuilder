#include "CoffArchive.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace ii::builder::detail {
namespace {
[[noreturn]] void bad(const std::string& why) { throw std::runtime_error(why); }
struct Reader final {
    std::ifstream input;
    std::uint64_t size;
    explicit Reader(const std::filesystem::path& path):input(path,std::ios::binary),size(0) {
        if(!input)bad("Cannot open COFF library");
        input.seekg(0,std::ios::end);const auto end=input.tellg();
        if(end<0)bad("Cannot determine COFF library size");size=static_cast<std::uint64_t>(end);
    }
    std::vector<unsigned char> get(std::uint64_t at,std::size_t count) {
        if(at>size || count>size-at || at>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
            bad("Truncated COFF archive");
        std::vector<unsigned char> bytes(count);input.clear();input.seekg(static_cast<std::streamoff>(at));
        if(count && !input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(count)))bad("Cannot read COFF archive");
        return bytes;
    }
};
std::uint64_t number(const std::vector<unsigned char>& b,std::size_t at,unsigned n) {
    if(at>b.size() || n>b.size()-at)bad("Truncated COFF structure");
    std::uint64_t value=0;for(unsigned i=0;i<n;++i)value|=std::uint64_t(b[at+i])<<(8*i);return value;
}
bool machine(std::uint64_t value) {
    return value==0x14c || value==0x8664 || value==0x1c0 || value==0x1c4 || value==0xaa64 || value==0xa641 || value==0xa64e;
}
std::string trim(std::string value) {
    while(!value.empty() && (value.back()==' ' || value.back()=='\0'))value.pop_back();return value;
}
std::string field(const std::vector<unsigned char>& value,std::size_t start,std::size_t count) {
    return trim(std::string(reinterpret_cast<const char*>(value.data()+start),count));
}
struct ObjectKind final { bool static_code=false, short_import=false, descriptor=false; };
ObjectKind object(Reader& r,std::uint64_t start,std::uint64_t size) {
    if(size<20)bad("COFF object member is too short");
    auto header=r.get(start,20);
    if(number(header,0,2)==0 && number(header,2,2)==0xffff) {
        const auto version=number(header,4,2),arch=number(header,6,2);
        if(!machine(arch))bad("Invalid COFF import machine");
        if(version>=2) {
            if(size<56)bad("Truncated COFF big object");
            return {.static_code=true};
        }
        if(version!=0)bad("Unsupported COFF import version");
        const auto length=number(header,12,4),types=number(header,18,2);
        if(length<3 || length>1024*1024 || length>size-20 || (types&0xffe0) || (types&3)>2 || ((types>>2)&7)>4)
            bad("Invalid COFF short import header");
        auto names=r.get(start+20,static_cast<std::size_t>(length));
        const auto symbol_end=std::find(names.begin(),names.end(),0);
        if(symbol_end==names.begin() || symbol_end==names.end())bad("Missing COFF import symbol");
        const auto dll_begin=symbol_end+1;
        const auto dll_end=std::find(dll_begin,names.end(),0);
        if(dll_begin==dll_end || dll_end==names.end())bad("Missing COFF import DLL name");
        std::string dll(dll_begin,dll_end);
        std::ranges::transform(dll,dll.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!dll.ends_with(".dll"))bad("COFF import does not name a DLL");
        return {.short_import=true};
    }
    const auto arch=number(header,0,2),sections=number(header,2,2),optional=number(header,16,2);
    if(!machine(arch) || !sections || sections>32768 || optional || sections>(size-20)/40)
        bad("Invalid COFF object header");
    bool import_sections=false,other_sections=false;
    for(std::uint64_t i=0;i<sections;++i) {
        auto sec=r.get(start+20+i*40,40);const auto name=field(sec,0,8);
        const auto length=number(sec,16,4),offset=number(sec,20,4);
        if(length && (offset>size || length>size-offset))bad("Invalid COFF section range");
        if(name.starts_with(".idata"))import_sections=true;
        else if(length && !name.starts_with(".debug") && name!=".drectve" && name!=".llvm_addrsig")other_sections=true;
    }
    bool descriptor=false;
    const auto sym_offset=number(header,8,4),sym_count=number(header,12,4);
    if(sym_count) {
        if(sym_count>1000000 || sym_offset>size || sym_count>(size-sym_offset)/18)bad("Invalid COFF symbol table");
        const auto strings=sym_offset+sym_count*18;
        if(strings>size || size-strings<4)bad("Missing COFF string table");
        const auto string_size=number(r.get(start+strings,4),0,4);
        if(string_size<4 || string_size>size-strings)bad("Invalid COFF string table");
        for(std::uint64_t i=0;i<sym_count;) {
            const auto symbol=r.get(start+sym_offset+i*18,18);
            std::string name;
            if(number(symbol,0,4)==0) {
                const auto offset=number(symbol,4,4);
                if(offset>=4 && offset<string_size) {
                    const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(512,string_size-offset));
                    auto bytes=r.get(start+strings+offset,n);auto end=std::find(bytes.begin(),bytes.end(),0);
                    if(end==bytes.end())bad("Unterminated COFF symbol name");name.assign(bytes.begin(),end);
                }
            } else name=field(symbol,0,8);
            const auto section=number(symbol,12,2),storage=number(symbol,16,1),aux=number(symbol,17,1);
            if(storage==2 && section>0 && section<0x8000 &&
               (name.starts_with("__IMPORT_DESCRIPTOR_") || name=="__NULL_IMPORT_DESCRIPTOR"))descriptor=true;
            if(aux>sym_count-i-1)bad("Invalid COFF auxiliary symbols");i+=aux+1;
        }
    }
    return {.static_code=other_sections || !import_sections,.descriptor=descriptor};
}
} // namespace
core::Result<CoffLibraryKind> inspect_coff_library(const std::filesystem::path& file) try {
    Reader r(file);const auto magic=r.get(0,8);
    if(std::string(magic.begin(),magic.end())!="!<arch>\n")bad("Not a COFF archive");
    std::uint64_t at=8;bool stat=false,imports=false,descriptor=false;std::size_t members=0;
    while(at<r.size) {
        const auto header=r.get(at,60);if(header[58]!='`' || header[59]!='\n')bad("Invalid COFF archive header");
        const auto name=field(header,0,16),raw_size=field(header,48,10);std::uint64_t length=0;
        const auto parsed=std::from_chars(raw_size.data(),raw_size.data()+raw_size.size(),length);
        if(raw_size.empty() || parsed.ec!=std::errc{} || parsed.ptr!=raw_size.data()+raw_size.size())bad("Invalid COFF member size");
        at+=60;if(length>r.size-at)bad("Truncated COFF member");
        if(name!="/" && name!="//" && name!="/SYM64/" && !name.starts_with("__.SYMDEF")) {
            const auto kind=object(r,at,length);stat|=kind.static_code;imports|=kind.short_import;descriptor|=kind.descriptor;
            if(++members>1000000)bad("Too many COFF members");
        }
        at+=length;if(length&1) { if(at>=r.size)bad("Missing COFF archive alignment");++at; }
    }
    if(!members || (!stat && !imports && !descriptor))bad("Ambiguous COFF library");
    if(stat && (imports||descriptor))return CoffLibraryKind::mixed;
    return stat?CoffLibraryKind::static_library:CoffLibraryKind::import_library;
} catch(const std::exception& error) {
    return core::fail(core::ErrorCode::invalid_argument,error.what());
}
} // namespace ii::builder::detail
