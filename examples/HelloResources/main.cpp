#include "hello_assets.h"
#include <iostream>
#include <stdexcept>
int main() {
    try {
        auto pack=ii::resource::embedded::load_hello_assets();
        if (!pack) throw std::runtime_error(pack.error().message);
        auto greeting=pack->find("text/greeting.txt"),alias=pack->find("text/greeting-alias.txt");
        auto target=pack->find("platform.txt"),icon=pack->find("icons/app.svg");
        if (!greeting || !alias || !target || !icon || !greeting->verify() || !target->verify() || !icon->verify())
            throw std::runtime_error("Native asset lookup or fingerprint verification failed");
        const auto deduplicated=greeting->bytes().data()==alias->bytes().data();
        const auto embedded=pack->stats().storage==ii::resource::StorageKind::embedded;
        if (!deduplicated || !embedded) throw std::runtime_error("Native storage contract failed");
        std::cout.write(reinterpret_cast<const char*>(greeting->bytes().data()),greeting->bytes().size());
        std::cout<<"NATIVE_RESOURCES_OK embedded="<<std::boolalpha<<embedded<<" deduplicated="<<deduplicated
            <<" entries="<<pack->stats().entries<<" target=";
        std::cout.write(reinterpret_cast<const char*>(target->bytes().data()),target->bytes().size());
        std::cout<<"icon_bytes="<<icon->bytes().size()<<'\n';return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
