#include <algorithm>
#include <iiSDKBuilder/iiSDKBuilder.h>
#include <iiSDKCore/Embedded/Embedded.h>
#include <iostream>
int main() {
    ii::embedded::EventLoop<2> loop;
    unsigned calls = 0;
    const auto ticket = loop.post({[](void *value) noexcept { ++*static_cast<unsigned *>(value); }, &calls});
    auto vehicle = ii::builder::parse_platform("automotive");
    ii::builder::Kit kit{.id = "installed-softcore", .platform = ii::builder::Platform::fpga};
    const auto applied = ii::builder::apply_target_profile(kit, "fpga-softcore");
    const auto profiles = ii::builder::target_profiles();
    if (!ticket || loop.poll(0) != 1 || calls != 1 || ii::embedded::contract_version() != 1 || !vehicle || !applied ||
        ii::builder::execution_model(kit) != ii::builder::ExecutionModel::native || profiles.size() < 50 ||
        ii::builder::default_kits().size() != 6)
        return 1;
    ii::builder::HlsRequest hls;
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto synthesis = ii::builder::synthesize_hls(hls, cancelled.get_token());
    if (synthesis || synthesis.error().code != ii::core::ErrorCode::cancelled ||
        std::ranges::none_of(profiles, [](const auto &p) { return p.id == "fpga-hls"; }))
        return 2;
    std::cout << "INSTALLED_HLS_API_OK\n";
    std::cout << "INSTALLED_EMBEDDED_CONSUMER_OK profiles=" << profiles.size() << '\n';
}
