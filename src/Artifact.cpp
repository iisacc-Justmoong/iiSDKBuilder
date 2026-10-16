#include <iiSDKBuilder/Builder.h>
#include "Json.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace ii::builder {
namespace {
[[noreturn]] void reject(const std::string& message) { throw std::runtime_error(message); }
struct Image final {
    std::ifstream& input;
    std::uint64_t base,size;
    void range(std::uint64_t offset,std::uint64_t count) const {
        if (offset>size || count>size-offset) reject("Truncated or invalid binary image range");
    }
    std::uint64_t number(std::uint64_t offset,unsigned count,bool big=false) const {
        range(offset,count);
        std::array<unsigned char,8> bytes{};
        input.clear();input.seekg(static_cast<std::streamoff>(base+offset));
        if (!input.read(reinterpret_cast<char*>(bytes.data()),count)) reject("Cannot read binary header");
        std::uint64_t value=0;
        for (unsigned i=0;i<count;++i) value|=static_cast<std::uint64_t>(bytes[i])<<(8*(big?count-1-i:i));
        return value;
    }
    Image slice(std::uint64_t offset,std::uint64_t count) const { range(offset,count);return {input,base+offset,count}; }
};
std::string cpu(std::uint64_t machine,BinaryFormat format) {
    if (format==BinaryFormat::pe) {
        switch (machine) { case 0x14c:return "x86";case 0x8664:return "x64";case 0x1c4:return "arm32";case 0xaa64:return "arm64"; }
    } else if (format==BinaryFormat::elf) {
        switch (machine) { case 3:return "x86";case 62:return "x64";case 40:return "arm32";case 183:return "arm64"; }
    } else {
        switch (machine) { case 7:return "x86";case 0x01000007:return "x64";case 12:return "arm32";case 0x0100000c:return "arm64"; }
    }
    reject("Unsupported native CPU machine identifier: "+std::to_string(machine));
}
bool is64(std::string_view arch) { return arch=="x64" || arch=="arm64"; }
std::string lower(std::string text) {
    std::ranges::transform(text,text.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::string architecture(std::string value) {
    value=lower(std::move(value));
    if (value=="arm64" || value=="arm64e" || value=="aarch64" || value=="arm64-v8a") return "arm64";
    if (value=="x64" || value=="amd64" || value=="x86_64" || value=="x86_64h") return "x64";
    if (value=="x86" || value=="i386" || value=="i686" || value=="win32") return "x86";
    if (value=="arm32" || value=="arm" || value.starts_with("armv7") || value=="armeabi-v7a") return "arm32";
    reject("Unsupported requested native architecture: "+value);
}
std::vector<std::string> pe(const Image& image) {
    if (image.number(0,2)!=0x5a4d) reject("Windows target requires a native PE image");
    const auto start=image.number(0x3c,4);
    if (image.number(start,4)!=0x00004550) reject("Invalid PE signature");
    const auto coff=start+4,optional=coff+20;
    image.range(coff,20);
    const auto arch=cpu(image.number(coff,2),BinaryFormat::pe);
    const auto sections=image.number(coff+2,2),optional_size=image.number(coff+16,2);
    if (!(image.number(coff+18,2)&2) || !sections || sections>96) reject("PE image is not executable or has invalid sections");
    image.range(optional,optional_size);
    const auto magic=image.number(optional,2);
    const bool bits64=magic==0x20b;
    if ((!bits64 && magic!=0x10b) || bits64!=is64(arch)) reject("PE CPU and optional-header class mismatch");
    const std::uint64_t directories=bits64?112:96;
    if (optional_size<directories) reject("Truncated PE optional header");
    const auto count=image.number(optional+directories-4,4);
    if (count>(optional_size-directories)/8) reject("Invalid PE data-directory count");
    if (count>14 && (image.number(optional+directories+14*8,4) || image.number(optional+directories+14*8+4,4)))
        reject("CLR/.NET managed PE requires a common runtime and is forbidden for native targets");
    const auto table=optional+optional_size;
    image.range(table,sections*40);
    bool code=false;
    for (std::uint64_t i=0;i<sections;++i) {
        const auto s=table+i*40;
        const auto length=image.number(s+16,4),offset=image.number(s+20,4),flags=image.number(s+36,4);
        if (length) image.range(offset,length);
        code=code || (length && (flags&0x20000000));
    }
    if (!code) reject("PE image contains no native executable section");
    return {arch};
}
std::vector<std::string> elf(const Image& image,const Kit& kit) {
    if (image.number(0,4,true)!=0x7f454c46) reject("Linux/Android target requires a native ELF image");
    const auto kind=image.number(4,1),order=image.number(5,1);
    if ((kind!=1 && kind!=2) || (order!=1 && order!=2) || image.number(6,1)!=1) reject("Invalid ELF identification");
    const bool bits64=kind==2,big=order==2;
    const auto header=bits64?64u:52u;
    image.range(0,header);
    const auto type=image.number(16,2,big);
    const auto arch=cpu(image.number(18,2,big),BinaryFormat::elf);
    if ((type!=2 && type!=3) || image.number(20,4,big)!=1 || bits64!=is64(arch)) reject("ELF image is not native executable/shared code or has a CPU/class mismatch");
    if (kit.platform==Platform::android && big) reject("Android ABI requires little-endian ELF");
    if (image.number(bits64?52:40,2,big)!=header) reject("Invalid ELF header size");
    const auto table=image.number(bits64?32:28,bits64?8:4,big);
    const auto entry=image.number(bits64?54:42,2,big),count=image.number(bits64?56:44,2,big);
    if (entry<(bits64?56u:32u) || !count) reject("ELF image has no valid program headers");
    image.range(table,count*entry);
    bool code=false;
    for (std::uint64_t i=0;i<count;++i) {
        const auto p=table+i*entry;
        if (image.number(p,4,big)!=1) continue;
        const auto flags=image.number(p+(bits64?4:24),4,big);
        const auto offset=image.number(p+(bits64?8:4),bits64?8:4,big);
        const auto length=image.number(p+(bits64?32:16),bits64?8:4,big);
        image.range(offset,length);
        code=code || (length && (flags&1));
    }
    if (!code) reject("ELF image contains no native executable load segment");
    return {arch};
}
std::string thin_mach(const Image& image,const Kit& kit) {
    const auto magic=image.number(0,4,true);
    const bool bits64=magic==0xfeedfacf || magic==0xcffaedfe;
    const bool big=magic==0xfeedface || magic==0xfeedfacf;
    if (!bits64 && magic!=0xfeedface && magic!=0xcefaedfe) reject("Apple target requires a native Mach-O image");
    const auto header=bits64?32u:28u;
    image.range(0,header);
    const auto arch=cpu(image.number(4,4,big),BinaryFormat::mach_o);
    if (bits64!=is64(arch)) reject("Mach-O CPU and header class mismatch");
    const auto type=image.number(12,4,big);
    if (type!=2 && type!=6 && type!=8) reject("Mach-O image is not executable/shared/bundle code");
    const auto count=image.number(16,4,big),length=image.number(20,4,big);
    if (!count || count>65536 || length>4*1024*1024) reject("Invalid Mach-O load-command bounds");
    image.range(header,length);
    std::uint64_t offset=header;
    unsigned platform=0;
    bool code=false;
    for (std::uint64_t i=0;i<count;++i) {
        if (offset>header+length || header+length-offset<8) reject("Truncated Mach-O load command");
        const auto command=image.number(offset,4,big),size=image.number(offset+4,4,big);
        if (size<8 || size>header+length-offset) reject("Invalid Mach-O load-command size");
        if (command==0x32) { if (size<24) reject("Truncated Mach-O build version");platform=static_cast<unsigned>(image.number(offset+8,4,big)); }
        else if (command==0x24) platform=1;
        else if (command==0x25) platform=(arch=="x86" || arch=="x64")?7:2;
        if (command==0x19 || command==1) {
            const bool segment64=command==0x19;
            if (size<(segment64?72u:56u)) reject("Truncated Mach-O segment command");
            const auto fileoff=image.number(offset+(segment64?40:32),segment64?8:4,big);
            const auto filesize=image.number(offset+(segment64?48:36),segment64?8:4,big);
            const auto protection=image.number(offset+(segment64?60:44),4,big);
            image.range(fileoff,filesize);
            code=code || (filesize && (protection&4));
        }
        offset+=size;
    }
    if (offset!=header+length) reject("Mach-O load-command length mismatch");
    if (!code) reject("Mach-O image contains no native executable segment");
    const auto expected=kit.platform==Platform::macos?1u:
        (lower(kit.sysroot).find("iphonesimulator")!=std::string::npos?7u:2u);
    if (platform!=expected) reject("Mach-O deployment platform does not match the macOS/iOS device/simulator kit");
    return arch;
}
std::vector<std::string> mach(const Image& image,const Kit& kit) {
    const auto magic=image.number(0,4,true);
    const bool fat64=magic==0xcafebabf || magic==0xbfbafeca;
    const bool fat=magic==0xcafebabe || magic==0xbebafeca || fat64;
    if (!fat) return {thin_mach(image,kit)};
    const bool big=magic==0xcafebabe || magic==0xcafebabf;
    const auto count=image.number(4,4,big);
    const std::uint64_t entry=fat64?32u:20u;
    if (!count || count>32) reject("Invalid universal Mach-O architecture count");
    image.range(8,count*entry);
    std::set<std::string> architectures;
    for (std::uint64_t i=0;i<count;++i) {
        const auto p=8+i*entry,offset=image.number(p+8,fat64?8:4,big),size=image.number(p+(fat64?16:12),fat64?8:4,big);
        if (offset<8+count*entry) reject("Universal Mach-O slice overlaps its header");
        auto arch=thin_mach(image.slice(offset,size),kit);
        if (arch!=cpu(image.number(p,4,big),BinaryFormat::mach_o) || !architectures.insert(arch).second)
            reject("Universal Mach-O architecture mismatch or duplicate slice");
    }
    return {architectures.begin(),architectures.end()};
}
void requested_cpu(const ArtifactInfo& info,const Kit& kit) {
    const auto requested=kit.platform==Platform::android?kit.android_abi:kit.architecture;
    if (requested.empty()) return;
    std::string_view rest=requested;
    while (!rest.empty()) {
        const auto end=rest.find(';');
        const auto expected=architecture(std::string(rest.substr(0,end)));
        if (std::ranges::find(info.architectures,expected)==info.architectures.end())
            reject("Native image CPU does not match requested architecture/ABI: "+expected);
        if (end==std::string_view::npos) break;
        rest.remove_prefix(end+1);
    }
}
}

ExecutionModel execution_model(Platform platform) { return platform==Platform::wasm?ExecutionModel::webassembly:ExecutionModel::native; }
std::string_view execution_model_name(ExecutionModel model) { return model==ExecutionModel::native?"native":"webassembly"; }
std::string_view binary_format_name(BinaryFormat format) {
    switch (format) { case BinaryFormat::pe:return "pe";case BinaryFormat::elf:return "elf";case BinaryFormat::mach_o:return "mach-o";case BinaryFormat::webassembly:return "webassembly"; }
    return "unknown";
}
core::Result<ArtifactInfo> inspect_artifact(const std::filesystem::path& file,const Kit& kit) {
    try {
        auto actual=file;
        if (kit.platform==Platform::wasm && actual.extension()!=".wasm") {
            if (actual.extension()!=".js" && actual.extension()!=".html") reject("WebAssembly target requires a .wasm module or its JS/HTML glue");
            actual.replace_extension(".wasm");
        }
        const auto size=std::filesystem::file_size(actual);
        if (size>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) reject("Binary image is too large to inspect");
        std::ifstream input(actual,std::ios::binary);
        if (!input) reject("Cannot open binary image");
        const Image image{input,0,size};
        ArtifactInfo info{actual,BinaryFormat::pe,execution_model(kit.platform),{}};
        switch (kit.platform) {
        case Platform::windows: info.format=BinaryFormat::pe;info.architectures=pe(image);break;
        case Platform::linux_os:case Platform::android: info.format=BinaryFormat::elf;info.architectures=elf(image,kit);break;
        case Platform::macos:case Platform::ios: info.format=BinaryFormat::mach_o;info.architectures=mach(image,kit);break;
        case Platform::wasm:
            if (image.number(0,4,true)!=0x0061736d || image.number(4,4)!=1) reject("Invalid WebAssembly module magic/version; JS or asm.js alone is forbidden");
            info.format=BinaryFormat::webassembly;info.architectures={"wasm"};break;
        default:reject("Invalid target platform");
        }
        if (kit.platform!=Platform::wasm) requested_cpu(info,kit);
        return info;
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::unsupported,"Artifact "+detail::path_string(file)+": "+e.what());
    }
}
} // namespace ii::builder
