#include <iiSDKBuilder/Builder.h>
#include <iiSDKCore/Graph/DependencyGraph.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include "Json.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <stdexcept>

namespace ii::builder {
namespace {
namespace fs=std::filesystem;
using detail::path_string;
[[noreturn]] void invalid(const std::string& message) { throw std::invalid_argument(message); }
std::string lower(std::string text) {
    for (auto& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool identifier(std::string_view value) {
    return !value.empty() && value!="." && value!=".." && value.size()<=128 &&
        std::ranges::all_of(value,[](unsigned char c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'; });
}
bool portable_name(const std::string& value) {
    if (!identifier(value) || value.back()=='.') return false;
    const auto base=lower(value.substr(0,value.find('.')));
    if (base=="con" || base=="prn" || base=="aux" || base=="nul") return false;
    if (base.size()==4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3]>='1' && base[3]<='9') return false;
    return true;
}
void no_nul(std::string_view value) { if (value.find('\0')!=std::string_view::npos) invalid("NUL is not allowed in process arguments"); }
fs::path normalized_path(const fs::path& path) {
    no_nul(path_string(path));
    return fs::weakly_canonical(fs::absolute(path));
}
bool within(const fs::path& path,const fs::path& root) {
    auto p=path.begin();
    for (auto r=root.begin();r!=root.end();++r,++p) {
        if (p==path.end()) return false;
#ifdef _WIN32
        if (lower(path_string(*p))!=lower(path_string(*r))) return false;
#else
        if (*p!=*r) return false;
#endif
    }
    return true;
}
std::string environment(const Kit& kit,const std::string& name) {
    if (const auto i=kit.environment.find(name);i!=kit.environment.end()) return i->second;
    const auto* value=std::getenv(name.c_str());
    return value?value:"";
}
fs::path executable(const fs::path& input) {
    std::error_code ec;
    auto found=[&](const fs::path& p) -> fs::path {
        if (fs::is_regular_file(p,ec)) return normalized_path(p);
#ifdef _WIN32
        if (p.extension().empty() && fs::is_regular_file(p.wstring()+L".exe",ec)) return normalized_path(p.wstring()+L".exe");
#endif
        return {};
    };
    if (input.has_parent_path()) return found(input);
    const auto* raw=std::getenv("PATH");
    std::string_view paths=raw?raw:"";
#ifdef _WIN32
    constexpr char separator=';';
#else
    constexpr char separator=':';
#endif
    while (!paths.empty()) {
        const auto end=paths.find(separator);
        if (auto p=found(detail::utf8_path(paths.substr(0,end))/input);!p.empty()) return p;
        if (end==std::string_view::npos) break;
        paths.remove_prefix(end+1);
    }
    return {};
}
void cache_valid(const Variables& variables) {
    static const std::set<std::string> reserved={"CMAKE_TOOLCHAIN_FILE","CMAKE_SYSTEM_NAME","CMAKE_INSTALL_PREFIX",
        "CMAKE_PREFIX_PATH","CMAKE_BUILD_TYPE","CMAKE_GENERATOR","CMAKE_GENERATOR_PLATFORM",
        "CMAKE_OSX_ARCHITECTURES","CMAKE_OSX_SYSROOT","CMAKE_ANDROID_ARCH_ABI","ANDROID_ABI",
        "ANDROID_PLATFORM","CMAKE_SYSTEM_VERSION"};
    for (const auto& [key,value]:variables) {
        if (!identifier(key) || key.front()=='-' || reserved.contains(key)) invalid("Reserved or invalid CMake variable: "+key+"; use kit fields");
        no_nul(value);
    }
}
void validate(CMakeProject& project,const fs::path& root) {
    if (!portable_name(project.name)) invalid("Invalid project name: "+project.name);
    if (project.source.empty()) invalid("Missing source for "+project.name);
    project.source=normalized_path(project.source);
    if (!fs::is_regular_file(project.source/"CMakeLists.txt")) invalid("Missing CMakeLists.txt for "+project.name+": "+path_string(project.source));
    if (within(project.source,root)) invalid("Build root must not contain source: "+path_string(project.source));
    cache_valid(project.cache);
    for (const auto& target:project.targets) if (!identifier(target) || target.front()=='-') invalid("Invalid build target: "+target);
}
std::string preliminary(Kit& kit,const fs::path& cmake) {
    auto host=host_platform();
    if (!host) return host.error().message;
    if (cmake.empty()) return "CMake executable not found on PATH";
    if (kit.generator.empty()) kit.generator=kit.platform==Platform::ios?"Xcode":"Ninja";
    if (kit.generator=="Ninja" || kit.generator=="Ninja Multi-Config") {
        const auto make=kit.cache.find("CMAKE_MAKE_PROGRAM");
        if (executable(make==kit.cache.end()?fs::path{"ninja"}:detail::utf8_path(make->second)).empty()) return "Ninja executable not found";
    }
    if ((kit.platform==Platform::macos || kit.platform==Platform::ios) && *host!=Platform::macos)
        return "Apple targets require a macOS host with Xcode";
    if ((kit.platform==Platform::windows || kit.platform==Platform::linux_os) && kit.platform!=*host && kit.toolchain.empty())
        return "A target CMake toolchain (compiler and sysroot) is required for this desktop platform";
    if (kit.platform==Platform::ios) {
        if (executable("xcrun").empty()) return "xcrun not found; install/select Xcode";
        if (kit.architecture.empty()) kit.architecture="arm64";
        if (kit.sysroot.empty()) kit.sysroot="iphoneos";
    }
    if (kit.platform==Platform::android && kit.toolchain.empty()) {
        for (const auto* key:{"ANDROID_NDK_HOME","ANDROID_NDK_ROOT"}) {
            const auto root=environment(kit,key);
            if (root.empty()) continue;
            const auto candidate=detail::utf8_path(root)/"build/cmake/android.toolchain.cmake";
            if (fs::is_regular_file(candidate)) { kit.toolchain=normalized_path(candidate); break; }
        }
        if (kit.toolchain.empty()) return "Set ANDROID_NDK_HOME/ANDROID_NDK_ROOT or kit.toolchain to the NDK CMake toolchain";
    }
    if (kit.platform==Platform::wasm && kit.toolchain.empty()) {
        std::vector<fs::path> roots;
        if (auto root=environment(kit,"EMSDK");!root.empty()) roots.push_back(detail::utf8_path(root)/"upstream/emscripten");
        if (auto root=environment(kit,"EMSCRIPTEN");!root.empty()) roots.push_back(detail::utf8_path(root));
        if (auto emcc=executable("emcc");!emcc.empty()) roots.push_back(emcc.parent_path());
        for (const auto& root:roots) {
            const auto candidate=root/"cmake/Modules/Platform/Emscripten.cmake";
            if (fs::is_regular_file(candidate)) { kit.toolchain=normalized_path(candidate); break; }
        }
        if (kit.toolchain.empty()) return "Activate emsdk or set kit.toolchain to Emscripten.cmake";
    }
    if (!kit.toolchain.empty()) {
        kit.toolchain=normalized_path(kit.toolchain);
        if (!fs::is_regular_file(kit.toolchain)) return "CMake toolchain does not exist: "+path_string(kit.toolchain);
    }
    return {};
}
std::vector<std::string> prefix_command(const Request& r,const Kit& kit,std::vector<std::string> args) {
    std::vector<std::string> out{path_string(r.cmake),"-E","env","--unset=CMAKE_PREFIX_PATH"};
    for (const auto& [key,value]:kit.environment) out.push_back(key+"="+value);
    out.push_back("--"); out.push_back(path_string(r.cmake));
    out.insert(out.end(),args.begin(),args.end());
    return out;
}
ProjectPlan project_plan(const Request& r,const KitPlan& kp,const CMakeProject& p,bool sdk) {
    const auto kitroot=r.build_root/"kits"/kp.kit.id/r.configuration;
    ProjectPlan plan{p,kitroot/(sdk?"sdk":"app")/(sdk?p.name:"")/"build",{}};
    const auto& k=kp.kit;
    Variables vars=k.cache;
    for (const auto& [key,value]:p.cache) vars[key]=value;
    vars.try_emplace("BUILD_TESTING","OFF");
    vars.try_emplace("IISDKCORE_BUILD_EXAMPLES","OFF");
    vars.try_emplace("BUILD_SHARED_LIBS","OFF");
    // NDK/Emscripten distributions may omit clang-scan-deps. Ordinary SDK
    // headers/sources need no C++ module scanning; module projects opt in.
    vars.try_emplace("CMAKE_CXX_SCAN_FOR_MODULES","OFF");
    vars["CMAKE_BUILD_TYPE"]=r.configuration;
    vars["CMAKE_INSTALL_PREFIX"]=path_string(kp.prefix);
    vars["CMAKE_INSTALL_LIBDIR"]="lib";
    vars["CMAKE_FIND_USE_PACKAGE_REGISTRY"]="OFF";
    vars["CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY"]="OFF";
    std::string prefixes=path_string(kp.prefix);
    for (const auto& prefix:k.prefixes) prefixes+=';'+path_string(normalized_path(prefix));
    vars["CMAKE_PREFIX_PATH"]=prefixes;
    auto& includes=vars["CMAKE_PROJECT_TOP_LEVEL_INCLUDES"];
    if (!includes.empty()) includes+=';';
    includes+=path_string(plan.binary_dir/"iisdkbuilder-verify.cmake");
    if (!sdk || p.name!="iiSDKCore") vars["iiSDKCore_DIR"]=path_string(kp.prefix/"lib/cmake/iiSDKCore");
    if (!k.toolchain.empty()) vars["CMAKE_TOOLCHAIN_FILE"]=path_string(k.toolchain);
    if ((k.platform==Platform::macos || k.platform==Platform::ios) && !k.architecture.empty()) vars["CMAKE_OSX_ARCHITECTURES"]=k.architecture;
    if (!k.sysroot.empty()) vars["CMAKE_OSX_SYSROOT"]=k.sysroot;
    if (k.platform==Platform::ios) {
        vars["CMAKE_SYSTEM_NAME"]="iOS";
        vars.try_emplace("CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED","NO");
        vars.try_emplace("CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY","");
    }
    if (k.platform==Platform::android) {
        vars["ANDROID_ABI"]=k.android_abi;
        vars["ANDROID_PLATFORM"]="android-"+std::to_string(k.android_api);
    }
    std::vector<std::string> args{"-S",path_string(p.source),"-B",path_string(plan.binary_dir),"-G",k.generator};
    if (k.generator.starts_with("Visual Studio") && !k.architecture.empty()) { args.push_back("-A"); args.push_back(k.architecture); }
    for (const auto& [key,value]:vars) args.push_back("-D"+key+"="+value);
    const auto logroot=kitroot/"logs"/(sdk?"sdk":"app")/(sdk?p.name:"");
    plan.commands.push_back({Phase::configure,p.name,prefix_command(r,k,std::move(args)),logroot/"configure.log",r.timeout});
    args={"--build",path_string(plan.binary_dir),"--config",r.configuration,"--parallel",std::to_string(r.jobs)};
    if (!p.targets.empty()) { args.push_back("--target"); args.insert(args.end(),p.targets.begin(),p.targets.end()); }
    plan.commands.push_back({Phase::build,p.name,prefix_command(r,k,std::move(args)),logroot/"build.log",r.timeout});
    if (sdk) plan.commands.push_back({Phase::install,p.name,prefix_command(r,k,
        {"--install",path_string(plan.binary_dir),"--config",r.configuration,"--prefix",path_string(kp.prefix)}),logroot/"install.log",r.timeout});
    return plan;
}
}

std::string_view platform_name(Platform platform) {
    switch (platform) {
    case Platform::windows:return "windows";
    case Platform::macos:return "macos";
    case Platform::linux_os:return "linux";
    case Platform::android:return "android";
    case Platform::ios:return "ios";
    case Platform::wasm:return "wasm";
    }
    return "unknown";
}
core::Result<Platform> parse_platform(std::string_view name) {
    for (const auto p:{Platform::windows,Platform::macos,Platform::linux_os,Platform::android,Platform::ios,Platform::wasm})
        if (name==platform_name(p)) return p;
    return core::fail(core::ErrorCode::invalid_argument,"Unknown platform: "+std::string(name));
}
core::Result<Platform> host_platform() {
    switch (ii::runtime::current_platform()) {
    case ii::runtime::Platform::windows:return Platform::windows;
    case ii::runtime::Platform::macos:return Platform::macos;
    case ii::runtime::Platform::linux_os:return Platform::linux_os;
    default:return core::fail(core::ErrorCode::unsupported,"iiSDKBuilder requires a Windows, macOS or Linux build host");
    }
}
std::vector<Kit> default_kits() {
    std::vector<Kit> kits;
    for (const auto p:{Platform::windows,Platform::macos,Platform::linux_os,Platform::android,Platform::ios,Platform::wasm})
        kits.push_back({.id=std::string(platform_name(p)),.platform=p});
    return kits;
}
std::string_view phase_name(Phase phase) {
    switch (phase) { case Phase::configure:return "configure";case Phase::build:return "build";case Phase::install:return "install"; }
    return "unknown";
}
core::Result<Plan> make_plan(const Request& input) {
    try {
        Plan plan{input,{}};
        auto& r=plan.request;
        if (r.kits.empty() || r.kits.size()>256) invalid("Between 1 and 256 target kits are required");
        if (!r.jobs || r.jobs>1024 || !r.parallel_kits || r.parallel_kits>64 || r.timeout.count()<=0) invalid("jobs, parallelKits and timeoutSeconds must be positive and bounded (1024 jobs, 64 kits)");
        if (!portable_name(r.configuration)) invalid("Invalid CMake configuration");
        if (r.sdk_root.empty()) invalid("sdkRoot is required");
        r.sdk_root=normalized_path(r.sdk_root);
        if (r.app.source.empty()) invalid("App source is required");
        if (r.build_root.empty()) r.build_root=r.app.source/"build/iiSDKBuilder";
        r.build_root=normalized_path(r.build_root);
        if (std::ranges::none_of(r.build_root,[](const auto& part) { return part=="build"; })) invalid("Build root must be inside a directory named build");
        validate(r.app,r.build_root);
        if (r.build_root==r.app.source) invalid("Build root cannot be the app source");
        auto resolved_cmake=executable(r.cmake);
        if (!resolved_cmake.empty()) r.cmake=resolved_cmake;
        std::vector<CMakeProject> projects{{"iiSDKCore",r.sdk_root/"iiSDKCore",{},{},{}}};
        bool explicit_core=false;
        for (const auto& sdk:r.sdks) {
            if (sdk.name=="iiSDKCore") {
                if (explicit_core) invalid("Duplicate iiSDKCore source declaration");
                projects[0]=sdk;explicit_core=true;
            }
            else projects.push_back(sdk);
        }
        if (!projects[0].depends.empty()) invalid("iiSDKCore cannot depend on upper SDK modules");
        ii::graph::DependencyGraph graph(projects.size());
        std::vector<ii::graph::NodeId> nodes;
        std::map<std::string,std::size_t> indexes;
        for (std::size_t i=0;i<projects.size();++i) {
            validate(projects[i],r.build_root);
            if (!indexes.emplace(lower(projects[i].name),i).second) invalid("Duplicate SDK name: "+projects[i].name);
            auto node=graph.add(); if (!node) return std::unexpected(node.error());
            nodes.push_back(*node);
        }
        for (std::size_t i=1;i<projects.size();++i) {
            auto dependencies=projects[i].depends;
            dependencies.push_back("iiSDKCore");
            std::set<std::size_t> unique;
            for (const auto& name:dependencies) {
                const auto found=indexes.find(lower(name));
                if (found==indexes.end()) invalid("Unknown dependency "+name+" in "+projects[i].name);
                if (unique.insert(found->second).second) {
                    auto connected=graph.connect(nodes[found->second],nodes[i]);
                    if (!connected) return core::fail(connected.error().code,"SDK dependency cycle involving "+projects[i].name+" and "+name);
                }
            }
        }
        for (const auto& name:r.app.depends) if (!indexes.contains(lower(name))) invalid("Unknown application SDK dependency: "+name);
        auto order=graph.order(); if (!order) return std::unexpected(order.error());
        std::set<std::string> ids;
        for (auto kit:r.kits) {
            if (!portable_name(kit.id) || !ids.insert(lower(kit.id)).second) invalid("Invalid or duplicate kit id: "+kit.id);
            if (platform_name(kit.platform)=="unknown") invalid("Invalid target platform");
            cache_valid(kit.cache);
            for (const auto& [key,value]:kit.environment) {
                if (!identifier(key) || key.find_first_of(".-")!=std::string::npos || key=="CMAKE_PREFIX_PATH") invalid("Invalid or reserved child environment variable: "+key);
                no_nul(value);
            }
            no_nul(kit.architecture);no_nul(kit.sysroot);no_nul(kit.generator);
            if (!kit.sysroot.empty() && kit.platform!=Platform::ios && kit.platform!=Platform::macos) invalid("Use a toolchain file for non-Apple sysroots");
            if (kit.platform==Platform::android) {
                constexpr std::string_view abis[]{"arm64-v8a","armeabi-v7a","x86","x86_64"};
                if (kit.android_api<21 || kit.android_api>100 || std::ranges::find(abis,kit.android_abi)==std::end(abis)) invalid("Invalid Android API/ABI");
            }
            KitPlan kp{std::move(kit),{},{},{}};
            kp.prefix=r.build_root/"kits"/kp.kit.id/r.configuration/"prefix";
            kp.unavailable_reason=preliminary(kp.kit,resolved_cmake);
            if (!kp.kit.architecture.empty() && kp.kit.platform!=Platform::macos && kp.kit.platform!=Platform::ios &&
                !kp.kit.generator.starts_with("Visual Studio"))
                invalid("architecture requires an Apple target or Visual Studio generator; use androidAbi or a target toolchain for "+kp.kit.id);
            for (const auto node:*order) {
                const auto found=std::ranges::find(nodes,node);
                const auto index=static_cast<std::size_t>(std::distance(nodes.begin(),found));
                kp.projects.push_back(project_plan(r,kp,projects[index],true));
            }
            kp.projects.push_back(project_plan(r,kp,r.app,false));
            plan.kits.push_back(std::move(kp));
        }
        return plan;
    } catch (const std::exception& e) { return core::fail(core::ErrorCode::invalid_argument,e.what()); }
}
} // namespace ii::builder
