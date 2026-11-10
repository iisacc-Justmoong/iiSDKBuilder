#include <iiSDKCore/Plugin/Plugin.h>
#include <algorithm>
namespace {
void* create() noexcept {static int value=3;return &value;}
void destroy(void*) noexcept {}
std::int32_t invoke(void*,std::uint32_t,const std::uint8_t* in,std::uint64_t n,std::uint8_t* out,std::uint64_t cap,std::uint64_t* written) noexcept {
    if(n>cap)return 1;std::copy(in,in+n,out);*written=n;return 0;
}
const ii::plugin::Descriptor descriptor{sizeof(ii::plugin::Descriptor),ii::plugin::abi_version,"library",7,create,destroy,invoke};
}
extern "C" const ii::plugin::Descriptor* ii_sdk_plugin_v1() noexcept {return &descriptor;}
