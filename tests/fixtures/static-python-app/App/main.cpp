#include <iiSDKCore/Plugin/Plugin.h>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>

ii::plugin::Request input(std::int64_t value){
    auto bits=std::bit_cast<std::uint64_t>(value);ii::plugin::Request request;
    for(unsigned i=0;i<8;++i)request.input.push_back(std::byte((bits>>(8*i))&255));
    return request;
}
std::int64_t decode(const std::vector<std::byte>& bytes){
    std::uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=std::uint64_t(bytes[i])<<(8*i);
    return std::bit_cast<std::int64_t>(bits);
}
int main(){
    auto plugin=ii::plugin::Plugin::open_static("transform");if(!plugin)return 1;
    auto modulo=ii::plugin::Plugin::open_static("modulo");if(!modulo)return 5;
    auto first=plugin->invoke(input(-7)),second=plugin->invoke(input(4));
    if(!first||!second||decode(*first)!=-3||decode(*second)!=11)return 2;
    if(plugin->invoke(input(std::numeric_limits<std::int64_t>::max())))return 3;
    if(plugin->invoke({.input={std::byte{1}}}))return 4;
    auto negative=modulo->invoke(input(-7)),positive=modulo->invoke(input(7));
    if(!negative||!positive||decode(*negative)!=-1||decode(*positive)!=-2)return 6;
    std::cout<<"STATIC_PYTHON_APP_OK\n";
}
