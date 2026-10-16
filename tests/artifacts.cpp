#include <iiSDKBuilder/Builder.h>
#include "BinaryFixtures.h"
#include <iostream>
#include <stdexcept>

using namespace ii::builder;
namespace fs=std::filesystem;
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void verify(const fs::path& path,const fixture::Bytes& data,Kit kit,BinaryFormat format) {
    fixture::write(path,data);
    auto inspected=inspect_artifact(path,kit);
    if (!inspected) throw std::runtime_error(inspected.error().message);
    require(inspected->format==format && inspected->execution==execution_model(kit.platform),"binary format and execution model");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"output argument");
        const fs::path out=argv[1], image=out/"binary";
        fs::create_directories(out);
        Kit windows{.id="windows",.platform=Platform::windows};
        Kit linux_kit{.id="linux",.platform=Platform::linux_os};
        Kit mac{.id="macos",.platform=Platform::macos};
        Kit android{.id="android",.platform=Platform::android};
        Kit ios{.id="ios",.platform=Platform::ios,.architecture="arm64"};
        Kit wasm{.id="wasm",.platform=Platform::wasm};
        verify(image,fixture::pe(),windows,BinaryFormat::pe);
        verify(image,fixture::pe(false,false),windows,BinaryFormat::pe);
        verify(image,fixture::elf(false),linux_kit,BinaryFormat::elf);
        verify(image,fixture::elf(false,false),linux_kit,BinaryFormat::elf);
        verify(image,fixture::elf(),android,BinaryFormat::elf);
        verify(image,fixture::mach(),mac,BinaryFormat::mach_o);
        verify(image,fixture::mach(true),ios,BinaryFormat::mach_o);
        mac.architecture="arm64;x86_64";
        verify(image,fixture::universal(),mac,BinaryFormat::mach_o);
        auto universal=inspect_artifact(image,mac);
        require(universal && universal->architectures.size()==2,"universal CPU slices");
        fixture::write(image,fixture::mach());
        require(!inspect_artifact(image,mac),"missing requested universal CPU slice rejected");
        mac.architecture.clear();
        require(!inspect_artifact(image,ios),"macOS binary cannot satisfy iOS");
        require(!inspect_artifact(image,windows),"Mach-O cannot satisfy Windows");
        fixture::write(image,fixture::pe(true));
        require(!inspect_artifact(image,windows),"CLR managed PE rejected");
        fixture::write(image,fixture::pe(true,false));
        require(!inspect_artifact(image,windows),"32-bit CLR managed PE rejected");
        fixture::write(image,fixture::elf(false));
        require(!inspect_artifact(image,android),"Android ABI mismatch rejected");
        fixture::write(image,fixture::mach(true,true));
        require(!inspect_artifact(image,ios),"simulator binary cannot satisfy device kit");
        ios.sysroot="iphonesimulator";
        verify(image,fixture::mach(true,true),ios,BinaryFormat::mach_o);
        for (const auto& bytes:std::vector<fixture::Bytes>{{'#','!','/','b','i','n','/','s','h'},
                {'P','K',3,4},{'d','e','x','\n'},{0xca,0xfe,0xba,0xbe},{'M','Z'},{0x7f,'E','L','F'}}) {
            fixture::write(image,bytes);
            require(!inspect_artifact(image,windows) && !inspect_artifact(image,linux_kit) && !inspect_artifact(image,mac),
                "scripts, bytecode and truncated headers rejected");
        }
        const auto js=out/"web.js", module=out/"web.wasm";
        fixture::write(js,{'c','o','n','s','o','l','e',';'});
        fs::remove(module);
        require(!inspect_artifact(js,wasm),"JS without a wasm module is rejected");
        fixture::write(module,{0,0x61,0x73,0x6d,1,0,0,0});
        auto web=inspect_artifact(js,wasm);
        require(web && web->file==module && web->format==BinaryFormat::webassembly &&
            web->execution==ExecutionModel::webassembly,"actual wasm module classified separately");
        require(!inspect_artifact(module,mac),"WASM cannot satisfy native target");
        for (auto p:{Platform::windows,Platform::macos,Platform::linux_os,Platform::android,Platform::ios})
            require(execution_model(p)==ExecutionModel::native,"five native platform contracts");
        std::cout << "Native image formats, CPU/ABI, platform, managed-code rejection and WASM distinction: passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
