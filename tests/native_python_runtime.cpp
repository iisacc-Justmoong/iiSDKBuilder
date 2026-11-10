#include <iiSDKCore/Plugin/Plugin.h>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
extern "C" const ii::plugin::Descriptor* ii_sdk_plugin_v1() noexcept;

int main(){
    auto api=ii_sdk_plugin_v1();auto instance=api->create();
    auto run=[&](std::int64_t value,std::int64_t expected){
        auto bits=std::bit_cast<std::uint64_t>(value);std::array<std::uint8_t,8> in{},out{};
        for(unsigned i=0;i<8;++i)in[i]=static_cast<std::uint8_t>(bits>>(8*i));
        std::uint64_t size=0;const auto status=api->invoke(instance,0,in.data(),8,out.data(),8,&size);
        bits=0;for(unsigned i=0;i<8;++i)bits|=std::uint64_t(out[i])<<(8*i);
        return status==0&&size==8&&std::bit_cast<std::int64_t>(bits)==expected;
    };
    if(!run(-7,-3)||!run(4,11))return 1;
    auto bits=std::bit_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::array<std::uint8_t,8> in{},out{};for(unsigned i=0;i<8;++i)in[i]=static_cast<std::uint8_t>(bits>>(8*i));
    std::uint64_t size=0;if(api->invoke(instance,0,in.data(),8,out.data(),8,&size)==0)return 2;
    api->destroy(instance);std::cout<<"native Python arithmetic passed\n";
}
