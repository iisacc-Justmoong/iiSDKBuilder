#include <iiSDKCore/Plugin/Plugin.h>
#include <filesystem>
#include <iostream>
int main(){
    auto alpha=ii::plugin::Plugin::open_static("alpha");
    auto beta=ii::plugin::Plugin::load(std::filesystem::absolute("libbeta.dylib"));
    auto library=ii::plugin::Plugin::load(std::filesystem::absolute("library.dylib"));
    auto versioned=ii::plugin::Plugin::load(std::filesystem::absolute("libbeta.so.1"));
    if(!alpha||!beta||!library||!versioned||ii::plugin::Plugin::open_static("missing"))return 1;
    ii::plugin::Request input{.input={std::byte{1},std::byte{2},std::byte{3}}};
    const auto a=alpha->invoke(input),b=beta->invoke(input);
    if(!a||!b||!library->invoke(input)||!versioned->invoke(input)||*a!=std::vector{std::byte{3},std::byte{2},std::byte{1}}||
       *b!=std::vector{std::byte{2},std::byte{3},std::byte{4}})return 2;
    std::cout<<"STATIC_PLUGIN_APP_OK\n";
}
