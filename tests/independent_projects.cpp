#include <iiSDKBuilder/Builder.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ii::builder;
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string pathText(const fs::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
int main(int argc,char** argv) try {
    check(argc==5,"expected source, Core source, build root and CMake paths");
    const fs::path source=argv[1],core=argv[2],build=argv[3];
    Request request;
    request.app={"Hello",source/"tests/fixtures/app",{"iiSDKCore","Addon"},{},{"hello"}};
    request.sdk_root=core.parent_path();
    request.sdks={{"Addon",source/"tests/fixtures/addon",{},{},{}}};
    request.build_root=build;
    request.cmake=argv[4];
    const auto host=host_platform();
    check(bool(host),"host platform");
    request.kits={{.id="host",.platform=*host}};
    const auto plan=make_plan(request);
    check(bool(plan),"independent SDK plan");
    const auto& projects=plan->kits.front().projects;
    const auto addon=std::ranges::find_if(projects,[](const auto& p){return p.project.name=="Addon";});
    check(addon!=projects.end(),"selected SDK is present");
    const auto& args=addon->commands.front().arguments;
    check(std::ranges::none_of(args,[](const auto& arg){return arg.starts_with("-DiiSDKCore_DIR=");}),
          "independent SDK must not receive an implicit Core package path");
    const auto core_prefix=plan->kits.front().prefix/"iiSDKCore";
    const auto addon_prefix=plan->kits.front().prefix/"Addon";
    const auto& core_args=projects.front().commands.front().arguments;
    for (const auto& environment : {"CMAKE_PREFIX_PATH", "CMAKE_INCLUDE_PATH", "CMAKE_LIBRARY_PATH",
            "CMAKE_FRAMEWORK_PATH", "CMAKE_APPBUNDLE_PATH", "CPATH", "C_INCLUDE_PATH",
            "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "LIBRARY_PATH", "PKG_CONFIG_PATH",
            "PKG_CONFIG_LIBDIR", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
            "DYLD_FRAMEWORK_PATH", "LD_LIBRARY_PATH"}) {
        check(std::ranges::find(args,std::string("--unset=")+environment)!=args.end(),
              "SDK configure must clear inherited package, compiler and loader paths");
    }
    check(std::ranges::find(core_args,"-DCMAKE_PREFIX_PATH="+pathText(core_prefix))!=core_args.end(),
          "Core configure must see its own isolated prefix");
    check(std::ranges::find(args,"-DCMAKE_PREFIX_PATH="+pathText(addon_prefix))!=args.end(),
          "SDK configure must not search another selected SDK's prefix");
    check(std::ranges::find(args,"-DCMAKE_INSTALL_PREFIX="+pathText(addon_prefix))!=args.end(),
          "SDK configure must use its own install prefix");
    check(std::ranges::find(args,"-DCMAKE_FIND_NO_INSTALL_PREFIX=ON")!=args.end(),
          "CMake must not implicitly search the install prefix as a system prefix");
    check(std::ranges::find(addon->commands.back().arguments,pathText(addon_prefix))!=addon->commands.back().arguments.end(),
          "SDK install must use its own install prefix");
    check(addon->binary_dir==plan->kits.front().prefix.parent_path()/"sdk/Addon/isolated/build",
          "independent SDK must not reuse a shared-prefix CMake cache");
    const auto& app=projects.back();
    check(app.project.name=="Hello", "application follows the independently selected SDKs");
    check(std::ranges::any_of(app.commands.front().arguments,[](const auto& arg){return arg.starts_with("-DiiSDKCore_DIR=");}),
          "application may compose the selected Core package");
    const auto& app_args=app.commands.front().arguments;
    check(std::ranges::find(app_args,"-DCMAKE_FIND_NO_INSTALL_PREFIX=ON")!=app_args.end(),
          "application must not implicitly search the previous shared prefix");
    check(std::ranges::find(app_args,"-DCMAKE_PREFIX_PATH="+pathText(core_prefix)+";"+pathText(addon_prefix))!=app_args.end(),
          "application must receive the independent SDK prefixes");
    check(std::ranges::find(app_args,"-DiiSDKCore_DIR="+pathText(core_prefix/"lib/cmake/iiSDKCore"))!=app_args.end(),
          "application Core hint must point to Core's isolated prefix");
    check(app.binary_dir==plan->kits.front().prefix.parent_path()/"app/isolated/build",
          "application must not reuse a shared-prefix CMake cache");
    auto with_external=request;
    with_external.kits.front().prefixes={source/"resources"};
    const auto external_plan=make_plan(with_external);
    check(bool(external_plan),"plan with an explicit external prefix");
    const auto external=pathText(fs::weakly_canonical(fs::absolute(source/"resources")));
    const auto& external_sdk=external_plan->kits.front().projects[1].commands.front().arguments;
    check(std::ranges::find(external_sdk,"-DCMAKE_PREFIX_PATH="+pathText(addon_prefix)+";"+external)!=external_sdk.end(),
          "explicit external prefix follows the SDK's own prefix");
    const auto& external_app=external_plan->kits.front().projects.back().commands.front().arguments;
    check(std::ranges::find(external_app,"-DCMAKE_PREFIX_PATH="+pathText(core_prefix)+";"+pathText(addon_prefix)+";"+external)!=external_app.end(),
          "explicit external prefix follows the application SDK prefixes");
    auto with_environment=request;
    with_environment.kits.front().environment["CPATH"]="explicit-third-party-include";
    const auto environment_plan=make_plan(with_environment);
    check(bool(environment_plan),"plan with explicit child environment");
    const auto& environment_args=environment_plan->kits.front().projects[1].commands.front().arguments;
    const auto unset=std::ranges::find(environment_args,"--unset=CPATH");
    const auto supplied=std::ranges::find(environment_args,"CPATH=explicit-third-party-include");
    check(unset!=environment_args.end() && supplied!=environment_args.end() && unset<supplied,
          "explicit kit environment is applied after inherited path removal");
    for (const auto& dependency:{"iiSDKCore","Addon","missing"}) {
        auto invalid=request;
        invalid.sdks.front().depends={dependency};
        check(!make_plan(invalid),"SDK-to-SDK dependency must be rejected");
    }
    auto unknown_app=request;
    unknown_app.app.depends={"missing"};
    check(!make_plan(unknown_app),"unknown application SDK must be rejected");
    check(!fs::exists(build),"planning does not create files");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
