#include <iiSDKBuilder/Builder.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ii::builder;
namespace fs = std::filesystem;
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
int main(int argc,char** argv) try {
    check(argc==4,"expected fixture root, build root and CMake executable");
    const fs::path source=argv[1];
    Request request;
    request.app={"Consumer",source/"app",{"iiSDKCore","Addon"},{},{"independent_consumer"}};
    request.sdk_root=source/"sdk";
    request.sdks={{"Addon",source/"addon",{},{},{}}};
    request.build_root=argv[2];
    request.cmake=argv[3];
    const auto host=host_platform();
    check(bool(host),"desktop host required");
    request.kits={{.id="host",.platform=*host}};
    const auto plan=make_plan(request);
    check(bool(plan),plan ? "" : plan.error().message);
    const auto report=Builder{}.build(request);
    check(bool(report),report ? "" : report.error().message);
    check(report->succeeded,report->kits.empty() ? "missing kit report" : report->kits[0].message);
    check(report->kits.size()==1 && report->kits[0].steps.size()==8,"all SDK and consumer commands must complete");
    const auto& kit=plan->kits.front();
    check(fs::is_regular_file(kit.prefix/"iiSDKCore/lib/cmake/iiSDKCore/iiSDKCoreConfig.cmake"),"Core installed independently");
    check(fs::is_regular_file(kit.prefix/"Addon/lib/cmake/Addon/AddonConfig.cmake"),"Addon installed independently");
    check(report->kits[0].verified_artifacts.size()==1,"consumer artifact was built and inspected");
    check(report->kits[0].execution_status=="not_requested","consumer application must not be run");
    auto explicit_environment=request;
    explicit_environment.kits.front().id="explicit-environment";
    const auto include=(source/"third-party").generic_u8string();
    explicit_environment.kits.front().environment["CMAKE_INCLUDE_PATH"]=
        std::string(reinterpret_cast<const char*>(include.data()),include.size());
    explicit_environment.sdks.front().cache["TEST_EXPLICIT_INCLUDE"]="ON";
    const auto explicit_report=Builder{}.build(explicit_environment);
    check(bool(explicit_report),explicit_report ? "" : explicit_report.error().message);
    check(explicit_report->succeeded,
          explicit_report->kits.empty() ? "missing explicit-environment kit report" : explicit_report->kits[0].message);
    check(explicit_report->kits[0].execution_status=="not_requested",
          "explicit-environment consumer application must not be run");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
