#pragma once
#include <iiSDKBuilder/Builder.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace fixture {
using Bytes=std::vector<unsigned char>;
inline void word(Bytes& bytes,std::size_t offset,std::uint64_t value,unsigned count,bool big=false) {
    for (unsigned i=0;i<count;++i) bytes.at(offset+i)=static_cast<unsigned char>(value>>(8*(big?count-1-i:i)));
}
inline Bytes mach(bool ios=false,bool simulator=false,bool intel=false) {
    Bytes bytes(136);
    word(bytes,0,0xfeedfacf,4);
    word(bytes,4,intel?0x01000007:0x0100000c,4);
    word(bytes,12,2,4);
    word(bytes,16,2,4);
    word(bytes,20,96,4);
    word(bytes,32,0x19,4);word(bytes,36,72,4);
    word(bytes,32+48,136,8);word(bytes,32+60,5,4);
    word(bytes,104,0x32,4);
    word(bytes,108,24,4);
    word(bytes,112,ios?(simulator?7:2):1,4);
    return bytes;
}
inline Bytes universal() {
    Bytes bytes(320);
    word(bytes,0,0xcafebabe,4,true);word(bytes,4,2,4,true);
    word(bytes,8,0x0100000c,4,true);word(bytes,16,48,4,true);word(bytes,20,136,4,true);
    word(bytes,28,0x01000007,4,true);word(bytes,36,184,4,true);word(bytes,40,136,4,true);
    auto arm=mach(),intel=mach(false,false,true);
    std::copy(arm.begin(),arm.end(),bytes.begin()+48);
    std::copy(intel.begin(),intel.end(),bytes.begin()+184);
    return bytes;
}
inline Bytes elf(bool arm=true,bool bits64=true) {
    const std::size_t header=bits64?64:52, ph=bits64?56:32;
    Bytes bytes(header+ph+8);
    bytes[0]=0x7f;bytes[1]='E';bytes[2]='L';bytes[3]='F';
    bytes[4]=bits64?2:1;bytes[5]=1;bytes[6]=1;
    word(bytes,16,3,2);word(bytes,18,arm?(bits64?183:40):(bits64?62:3),2);word(bytes,20,1,4);
    word(bytes,bits64?32:28,header,bits64?8:4);
    word(bytes,bits64?52:40,header,2);word(bytes,bits64?54:42,ph,2);word(bytes,bits64?56:44,1,2);
    word(bytes,header,1,4);
    word(bytes,header+(bits64?4:24),5,4);
    word(bytes,header+(bits64?8:4),header+ph,bits64?8:4);
    word(bytes,header+(bits64?32:16),8,bits64?8:4);
    return bytes;
}
inline Bytes pe(bool managed=false,bool bits64=true) {
    const std::size_t opt=0x80+24, optsize=bits64?240:224, section=opt+optsize;
    Bytes bytes(section+48);
    bytes[0]='M';bytes[1]='Z';word(bytes,0x3c,0x80,4);
    bytes[0x80]='P';bytes[0x81]='E';word(bytes,0x84,bits64?0x8664:0x14c,2);
    word(bytes,0x86,1,2);word(bytes,0x94,optsize,2);word(bytes,0x96,0x2,2);
    word(bytes,opt,bits64?0x20b:0x10b,2);
    word(bytes,opt+(bits64?108:92),16,4);
    if (managed) { word(bytes,opt+(bits64?112:96)+14*8,0x1000,4);word(bytes,opt+(bits64?112:96)+14*8+4,72,4); }
    word(bytes,section+16,8,4);word(bytes,section+20,section+40,4);word(bytes,section+36,0x60000020,4);
    return bytes;
}
inline void write(const std::filesystem::path& path,const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
}
inline void host_image(const std::filesystem::path& path) {
    auto host=ii::builder::host_platform();
    write(path,*host==ii::builder::Platform::macos?mach():(*host==ii::builder::Platform::windows?pe():elf(false)));
}
} // namespace fixture
