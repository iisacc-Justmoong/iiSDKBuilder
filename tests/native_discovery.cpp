#include <iiSDKBuilder/NativeAot.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void write(const fs::path& file,const char* text){fs::create_directories(file.parent_path());std::ofstream(file)<<text;}
int main(int argc,char** argv){try{
    check(argc==2,"workspace required");const fs::path root=argv[1];
    write(root/"App/plugins/alpha.cpp",R"(extern "C" const void* ii_sdk_plugin_v1() noexcept { return nullptr; })");
    write(root/"App/plugins/beta.cc",R"(extern "C" const void* ii_sdk_plugin_v1() noexcept { return nullptr; })");
    write(root/"App/comments.cpp",R"cpp(
      // ii_sdk_plugin_v1() { this is a comment; }
      const char* x = "ii_sdk_plugin_v1() { text }";
      const char* y = R"tag(ii_sdk_plugin_v1() { raw text })tag";
      auto digits=1'000;auto hex=0xFF'FF;
      extern "C" const void* ii_sdk_plugin_v1() noexcept;
      void consume() { if (ii_sdk_plugin_v1()) { /* function call, not a definition */ } }
    )cpp");
    write(root/"App/tests/ignored.cpp",R"(extern "C" void* ii_sdk_plugin_v1(){return nullptr;})");
    write(root/"App/.hidden/ignored.cpp",R"(extern "C" void* ii_sdk_plugin_v1(){return nullptr;})");
    write(root/"App/compute.py","def transform(value: int) -> int:\n    return value + 1\n");
    write(root/"App/UPPER.PY","def transform(value: int) -> int:\n    return value + 1\n");
    write(root/"App/tests/ignored.py","raise RuntimeError()\n");
    write(root/"App/runtime.js","execute()\n");
    write(root/"App/tests/ignored.wasm","not runtime code\n");
    auto found=ii::builder::discover_static_plugins(root/"App");
    check(bool(found),"discovery must succeed");check(found->size()==2,"discover two definitions only");
    check((*found)[0].filename()=="alpha.cpp"&&(*found)[1].filename()=="beta.cc","discovery order must be deterministic");
    check(!ii::builder::discover_static_plugins(root/"missing"),"missing source root must be rejected");
    auto python=ii::builder::discover_static_python(root/"App");
    check(python&&python->size()==2,"discover Python source regardless of extension case");
    auto unsupported=ii::builder::discover_uncompiled_runtime(root/"App");
    check(unsupported&&unsupported->size()==1&&unsupported->front().filename()=="runtime.js","detect unsupported runtime code");
    std::error_code ec;fs::create_symlink(root/"App/compute.py",root/"App/alias.py",ec);
    if(!ec)check(!ii::builder::discover_static_python(root/"App"),"unresolved source symlinks must fail closed");
    std::cout<<"static plugin source discovery passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
