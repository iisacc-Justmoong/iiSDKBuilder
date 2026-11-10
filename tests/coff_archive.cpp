#include "CoffArchive.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ii::builder::detail;
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
void word(Bytes& bytes,std::size_t offset,unsigned long long value,unsigned length) {
    for(unsigned i=0;i<length;++i) bytes.at(offset+i)=static_cast<unsigned char>(value>>(i*8));
}
Bytes imported() {
    const std::string names("symbol\0module.dll\0",18);
    Bytes value(20+names.size());word(value,2,0xffff,2);word(value,6,0x8664,2);word(value,12,names.size(),4);
    std::copy(names.begin(),names.end(),value.begin()+20);return value;
}
Bytes object() {
    Bytes value(64);word(value,0,0x8664,2);word(value,2,1,2);
    value[20]='.';value[21]='t';value[22]='e';value[23]='x';value[24]='t';
    word(value,36,4,4);word(value,40,60,4);word(value,56,0x60000020,4);return value;
}
Bytes archive(const std::vector<Bytes>& members) {
    const std::string magic="!<arch>\n";Bytes value(magic.begin(),magic.end());
    for(const auto& member:members) {
        std::string header(60,' ');header.replace(0,8,"item.obj");
        auto count=std::to_string(member.size());header.replace(48,count.size(),count);header[58]='`';header[59]='\n';
        value.insert(value.end(),header.begin(),header.end());value.insert(value.end(),member.begin(),member.end());
        if(member.size()%2)value.push_back('\n');
    }
    return value;
}
void write(const fs::path& path,const Bytes& bytes) {
    fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
}
int main(int argc,char** argv) try {
    if(argc==3) {
        const auto actual=inspect_coff_library(argv[1]);
        if(!actual) throw std::runtime_error(actual.error().message);
        const std::string expected=argv[2];
        const auto kind=expected=="import" ? CoffLibraryKind::import_library :
                        expected=="static" ? CoffLibraryKind::static_library : CoffLibraryKind::mixed;
        check(*actual==kind,"real COFF archive classification");return 0;
    }
    check(argc==2,"workspace required");const fs::path root=argv[1];
    auto path=root/"sample.lib";
    write(path,archive({imported()}));auto result=inspect_coff_library(path);
    check(result && *result==CoffLibraryKind::import_library,"valid short import library");
    write(path,archive({object()}));result=inspect_coff_library(path);
    check(result && *result==CoffLibraryKind::static_library,"static COFF object");
    write(path,archive({object(),imported()}));result=inspect_coff_library(path);
    check(result && *result==CoffLibraryKind::mixed,"mixed library includes static code");
    auto broken=imported();word(broken,12,9999,4);write(path,archive({broken}));
    check(!inspect_coff_library(path),"truncated import data rejected");
    auto truncated=archive({object()});truncated.resize(truncated.size()-2);write(path,truncated);
    check(!inspect_coff_library(path),"truncated archive rejected");
    write(path,Bytes{'!','<','a','r','c','h','>','\n'});
    check(!inspect_coff_library(path),"empty archive cannot be classified");
    std::cout<<"COFF import/static/mixed classification passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
