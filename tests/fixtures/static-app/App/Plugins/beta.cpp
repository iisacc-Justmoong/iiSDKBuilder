#include <iiSDKCore/Plugin/Plugin.h>
namespace {
void* create() noexcept {static int value=2;return &value;}
void destroy(void*) noexcept {}
std::int32_t invoke(void*,std::uint32_t,const std::uint8_t* in,std::uint64_t n,std::uint8_t* out,std::uint64_t cap,std::uint64_t* written) noexcept {
    if(n>cap)return 1;for(std::uint64_t i=0;i<n;++i)out[i]=static_cast<std::uint8_t>(in[i]+1);*written=n;return 0;
}
const ii::plugin::Descriptor descriptor{sizeof(ii::plugin::Descriptor),ii::plugin::abi_version,"beta",4,create,destroy,invoke};
}
extern "C" const ii::plugin::Descriptor* ii_sdk_plugin_v1() noexcept {return &descriptor;}
