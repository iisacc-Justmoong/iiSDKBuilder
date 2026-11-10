#include "WasmDylink.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ii::builder::detail;
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void leb(Bytes& out,std::size_t value) { do { auto byte=static_cast<unsigned char>(value&127);value>>=7;out.push_back(byte|(value?128:0)); } while(value); }
void text(Bytes& out,const std::string& value) { leb(out,value.size());out.insert(out.end(),value.begin(),value.end()); }
Bytes module(const std::vector<std::string>& deps) {
    Bytes data;leb(data,deps.size());for(const auto& dep:deps)text(data,dep);
    Bytes sub;sub.push_back(2);leb(sub,data.size());sub.insert(sub.end(),data.begin(),data.end());
    Bytes payload;text(payload,"dylink.0");payload.insert(payload.end(),sub.begin(),sub.end());
    Bytes out{0,'a','s','m',1,0,0,0,0};leb(out,payload.size());out.insert(out.end(),payload.begin(),payload.end());return out;
}
void write(const fs::path& p,const Bytes& bytes) {fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
int main(int argc,char** argv) try {
    if(argc==3) { auto result=wasm_needed(argv[1]);require(bool(result),result?"":result.error().message.c_str());
        require(result->size()==1 && result->front()==argv[2],"real WebAssembly side dependency");return 0; }
    require(argc==2,"workspace required");const fs::path path=fs::path(argv[1])/"fixture.wasm";
    write(path,module({"used.so","sub/second.wasm"}));auto found=wasm_needed(path);
    require(found&&found->size()==2&&found->at(0)=="used.so"&&found->at(1)=="sub/second.wasm","Wasm dylink dependencies");
    write(path,module({"../escape.so"}));require(!wasm_needed(path),"traversal rejected");
    auto damaged=module({"used.so"});damaged.pop_back();write(path,damaged);require(!wasm_needed(path),"truncated section rejected");
    write(path,Bytes{0,'a','s','m',1,0,0,0});found=wasm_needed(path);require(found&&found->empty(),"ordinary Wasm accepted");
    std::cout<<"Wasm dynamic dependency parsing passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
