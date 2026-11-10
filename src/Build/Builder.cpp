#include <iiFileProviderStorage.h>
#include <iiSDKBuilder/Builder.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include "Json.h"
#include "Targets.h"
#include "CoffArchive.h"
#include "WasmDylink.h"
#include "PackageScript.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <utility>
#include <fstream>
#include <set>
#include <stdexcept>

namespace ii::builder {
namespace {
namespace fs=std::filesystem;
using detail::Json;
using detail::path_string;
core::Result<void> prepare(const ProjectPlan& project,const Kit& kit) {
    const auto verify=project.binary_dir/"iisdkbuilder-verify.cmake";
    const auto expected=detail::target_system(kit,project.host_validation);
    std::error_code ec;
    fs::remove(project.binary_dir/"iisdkbuilder-system.txt",ec);
    if (ec) return core::fail(core::ErrorCode::failed,"Cannot clear target verification marker: "+ec.message());
    // Deferred verification sees the final top-level project state and does not
    // add targets or require applications to include a builder-specific module.
    auto result=detail::write_text(verify,
        "if(IISDKBUILDER_PRUNE_UNUSED_SHARED)\n"
        "  if(CMAKE_SYSTEM_NAME STREQUAL \"Darwin\" OR CMAKE_SYSTEM_NAME STREQUAL \"iOS\")\n"
        "    add_link_options(\"LINKER:-dead_strip_dylibs\")\n"
        "  elseif(CMAKE_SYSTEM_NAME STREQUAL \"Linux\" OR CMAKE_SYSTEM_NAME STREQUAL \"Android\")\n"
        "    add_link_options(\"LINKER:--as-needed\")\n"
        "  endif()\n"
        "endif()\n"
        "function(_iisdkbuilder_verify_target)\n"
        "  if(NOT CMAKE_SYSTEM_NAME STREQUAL \""+expected+"\")\n"
        "    message(FATAL_ERROR \"iiSDKBuilder expected "+expected+", got ${CMAKE_SYSTEM_NAME}; check the kit toolchain\")\n"
        "  endif()\n"
        "  file(WRITE \"${CMAKE_BINARY_DIR}/iisdkbuilder-system.txt\" \"${CMAKE_SYSTEM_NAME}\\n${CMAKE_SYSTEM_PROCESSOR}\\n${CMAKE_OSX_ARCHITECTURES}\\n\")\n"
        "endfunction()\n"
        "cmake_language(DEFER CALL _iisdkbuilder_verify_target)\n");
    if (!result) return result;
    if(kit.platform==Platform::wasm) {
        result=detail::write_text(project.binary_dir/"iisdkbuilder-after-project.cmake",
            "if(EMSCRIPTEN AND CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)\n"
            "  set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)\n"
            "  add_compile_options(\"$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-sMAIN_MODULE=2>\"\n"
            "    \"$<$<OR:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>>:-sSIDE_MODULE=1>\")\n"
            "  add_link_options(\"$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-sMAIN_MODULE=2>\"\n"
            "    \"$<$<OR:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>>:-sSIDE_MODULE=1>\")\n"
            "endif()\n");
        if(!result)return result;
    }
    return detail::write_text(project.binary_dir/".cmake/api/v1/query/client-iiSDKBuilder/codemodel-v2","");
}
Json file_api_json(const fs::path& replies,const Json& reference) {
    const auto name=detail::utf8_path(reference.at("jsonFile").string());
    if (name.has_parent_path() || name.empty()) throw std::runtime_error("Invalid CMake File API reference");
    auto json=detail::read_json(replies/name);
    if (!json) throw std::runtime_error(json.error().message);
    return std::move(*json);
}
std::string artifact_path(std::string path,const Kit& kit) {
    // CMake's Xcode File API retains this build-setting placeholder for
    // embedded SDKs. Each kit selects exactly one SDK, so it has one suffix.
    std::string effective;
    if (kit.platform==Platform::ios) {
        auto sdk=kit.sysroot;
        std::ranges::transform(sdk,sdk.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        effective=sdk.find("iphonesimulator")!=std::string::npos?"-iphonesimulator":"-iphoneos";
    }
    for (const std::string token:{"${EFFECTIVE_PLATFORM_NAME}","$(EFFECTIVE_PLATFORM_NAME)"}) {
        std::size_t where=0;
        while ((where=path.find(token,where))!=std::string::npos) { path.replace(where,token.size(),effective);where+=effective.size(); }
    }
    return path;
}
bool archive_fragment(std::string_view text) {
    for (auto position=text.find(".a");position!=std::string_view::npos;position=text.find(".a",position+2)) {
        const auto after=position+2;
        if(after==text.size() || std::isspace(static_cast<unsigned char>(text[after])) ||
           text[after]=='"' || text[after]=='\\' || text[after]==',' || text[after]==')')return true;
    }
    return false;
}
std::vector<fs::path> sdk_library_paths(std::string fragment,const fs::path& prefix) {
    auto root=path_string(prefix);
    std::ranges::replace(fragment,'\\','/');
    std::ranges::replace(root,'\\','/');
    auto folded=fragment,folded_root=root;
    for(auto* value:{&folded,&folded_root})
        std::ranges::transform(*value,value->begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    std::vector<fs::path> result;
    if(folded_root.empty())return result;
    auto search=std::size_t{0};
    while(true) {
        const auto start=folded.find(folded_root,search);
        if(start==std::string::npos)break;
        search=start+folded_root.size();
        if(search<folded.size() && folded[search]!='/')continue;
        const auto next_root=folded.find(folded_root,search);
        const auto limit=next_root==std::string::npos?folded.size():next_root;
        for(auto position=search;position<limit;++position) {
            const auto extension=folded.compare(position,4,".lib")==0?4u:
                (folded.compare(position,2,".a")==0?2u:0u);
            if(!extension)continue;
            const auto end=position+extension;
            if(end>limit || (end<folded.size() && !std::isspace(static_cast<unsigned char>(folded[end])) &&
               folded[end]!='"' && folded[end]!=',' && folded[end]!=')'))continue;
            result.push_back(detail::utf8_path(fragment.substr(start,end-start)));
            search=end;
            break;
        }
    }
    return result;
}
bool enabled_option(const Variables& values,std::string_view name) {
    const auto found=values.find(std::string(name));
    if(found==values.end())return false;
    auto value=found->second;
    std::ranges::transform(value,value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(value=="on"||value=="true"||value=="yes"||value=="1")return true;
    if(value=="off"||value=="false"||value=="no"||value=="0")return false;
    throw std::runtime_error(std::string(name)+" must be ON or OFF");
}
std::vector<fs::path> artifacts(const ProjectPlan& app,const std::string& configuration,const Kit& kit,
                                std::vector<fs::path>& executables,const fs::path& sdk_prefix,
                                std::vector<std::string>& static_sdk_links) {
    const auto replies=app.binary_dir/".cmake/api/v1/reply";
    fs::path index;
    for (const auto& entry:fs::directory_iterator(replies)) {
        const auto name=entry.path().filename();
        if (path_string(name).starts_with("index-") && name.extension()==".json" && name>index.filename()) index=entry.path();
    }
    auto root=detail::read_json(index);
    if (!root) throw std::runtime_error("CMake File API index missing after app build");
    const auto& client=root->at("reply").at("client-iiSDKBuilder").at("codemodel-v2");
    const auto model=file_api_json(replies,client);
    std::set<fs::path> unique;
    for (const auto& config:model.at("configurations").array()) {
        const auto& name=config.at("name").string();
        if (!name.empty() && name!=configuration) continue;
        for (const auto& reference:config.at("targets").array()) {
            if (!app.project.targets.empty() && std::ranges::find(app.project.targets,reference.at("name").string())==app.project.targets.end()) continue;
            const auto target=file_api_json(replies,reference);
            if (target.has("type")) {
                const auto& type=target.at("type").string();
                if (type!="EXECUTABLE" && type!="SHARED_LIBRARY" && type!="MODULE_LIBRARY") continue;
            }
            if (target.has("type") && (target.at("type").string()=="EXECUTABLE" ||
                (kit.platform==Platform::android && target.at("type").string()=="SHARED_LIBRARY")) && target.has("link") &&
                target.at("link").has("commandFragments")) {
                for(const auto& fragment:target.at("link").at("commandFragments").array()) {
                    if(!fragment.has("fragment"))continue;
                    const auto& text=fragment.at("fragment").string();
                    if(kit.platform==Platform::windows) {
                        for(const auto& library:sdk_library_paths(text,sdk_prefix)) {
                            const auto kind=detail::inspect_coff_library(library);
                            if(!kind)throw std::runtime_error("Cannot classify Windows SDK library "+path_string(library)+": "+kind.error().message);
                            if(*kind!=detail::CoffLibraryKind::import_library)static_sdk_links.push_back(text);
                        }
                    } else if(text.find(path_string(sdk_prefix))!=std::string::npos && archive_fragment(text)) {
                        static_sdk_links.push_back(text);
                    }
                }
            }
            if (!target.has("artifacts")) continue;
            const auto primary=target.has("nameOnDisk")?
                detail::utf8_path(target.at("nameOnDisk").string()).filename():fs::path{};
            for (const auto& artifact:target.at("artifacts").array()) {
                auto p=detail::utf8_path(artifact_path(artifact.at("path").string(),kit));
                if (!primary.empty() && p.filename()!=primary &&
                    !(target.has("type") && target.at("type").string()=="EXECUTABLE" &&
                      (p.generic_string().find(".app/Contents/MacOS/")!=std::string::npos ||
                       (kit.platform==Platform::ios && p.generic_string().find(".app/")!=std::string::npos)))) continue; // PDB/import archives are auxiliary files.
                if (p.is_relative()) p=app.binary_dir/p;
                p=p.lexically_normal();
                if (fs::is_regular_file(p)) {
                    unique.insert(p);
                    if (target.has("type") && (target.at("type").string()=="EXECUTABLE" ||
                        (kit.platform==Platform::android && target.at("type").string()=="SHARED_LIBRARY"))) executables.push_back(p);
                }
            }
        }
    }
    if (unique.empty()) throw std::runtime_error("No existing app target artifacts in the CMake File API; specify artifact-producing app targets");
    return {unique.begin(),unique.end()};
}
struct RootLock final {
    fs::path path;
    bool owned=false;
    ~RootLock() { if (owned) { std::error_code ec; fs::remove(path,ec); } }
};
core::Result<void> kit_identity(const KitPlan& plan) {
    const auto& k=plan.kit;
    Json::Object environment,cache;
    for (const auto& [name,value]:k.environment) environment.emplace(name,value);
    for (const auto* name:{"CMAKE_C_COMPILER","CMAKE_CXX_COMPILER","CMAKE_C_COMPILER_TARGET",
        "CMAKE_CXX_COMPILER_TARGET","CMAKE_SYSROOT","CMAKE_MAKE_PROGRAM","ANDROID_STL"})
        if (const auto found=k.cache.find(name);found!=k.cache.end()) cache.emplace(name,found->second);
    std::string toolchain_time;
    if (!k.toolchain.empty()) toolchain_time=std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        fs::last_write_time(k.toolchain).time_since_epoch()).count());
    auto identity_object=Json::Object{{"platform",platform_name(k.platform)},
        {"toolchain",path_string(k.toolchain)},{"toolchainModified",toolchain_time},{"generator",k.generator},
        {"architecture",k.architecture},{"sysroot",k.sysroot},{"androidAbi",k.android_abi},
        {"androidApi",k.android_api},{"environment",std::move(environment)},{"compilerCache",std::move(cache)}};
    if(detail::extended_target(k)||!k.target.run.empty()||!k.target.deploy.empty()) {
        auto compile=k.target;compile.deploy.clear();compile.run.clear();compile.port.clear();compile.runner.clear();compile.expected_output.clear();compile.monitor=false;
        identity_object.emplace("target",detail::target_json(compile));
    }
    const auto identity=detail::dump_json(identity_object);
    const auto file=plan.prefix.parent_path()/"kit-identity.json";
    if (fs::exists(file)) {
        auto previous=detail::read_json(file);
        if (!previous || detail::dump_json(*previous)!=identity)
            return core::fail(core::ErrorCode::invalid_argument,"Kit target identity changed; use a new kit id or a new build root: "+k.id);
        return {};
    }
    return detail::write_text(file,identity);
}
KitReport build_kit(const KitPlan &kit, const Request &request, const Executor &executor, std::stop_token stop) {
    KitReport report{kit.kit.id, kit.kit.platform, Status::succeeded, {}, {}, {}};
    report.execution = execution_model(kit.kit);
    report.profile = kit.kit.target.profile;
    report.runner = kit.kit.target.runner;
    report.deployment =
        request.deploy || (request.run && kit.kit.target.adapter == "arduino" && !kit.kit.target.port.empty())
            ? "blocked"
            : "not_requested";
    report.execution_status = request.run ? "blocked" : "not_requested";
    if (stop.stop_requested()) {
        report.status = Status::cancelled;
        report.message = "Build cancelled before execution";
    } else if (!kit.unavailable_reason.empty()) {
        report.status = Status::unavailable;
        report.message = kit.unavailable_reason;
    }
    if (report.status == Status::succeeded) {
        auto identity = kit_identity(kit);
        if (!identity) {
            report.status = Status::failed;
            report.message = identity.error().message;
        }
    }
    auto execute = [&](const Command &command, const ProjectPlan *project = nullptr) {
        StepReport step{command, Status::blocked, -1, {}, report.message};
        if (report.status == Status::succeeded && stop.stop_requested()) {
            report.status = Status::cancelled;
            report.message = "Build cancelled before next command";
        }
        if (report.status != Status::succeeded) {
            step.message = report.message;
            report.steps.push_back(std::move(step));
            return false;
        }
        const auto started = std::chrono::steady_clock::now();
        if (command.phase == Phase::configure && project) {
            auto prepared = prepare(*project, kit.kit);
            if (!prepared) {
                report.status = step.status = Status::failed;
                report.message = step.message = prepared.error().message;
                report.steps.push_back(std::move(step));
                return false;
            }
        }
        auto result = [&]() -> core::Result<ProcessOutcome> {
            try {
                return executor(command, stop);
            } catch (const std::exception &e) {
                return core::fail(core::ErrorCode::failed, "Executor threw: " + std::string(e.what()));
            } catch (...) {
                return core::fail(core::ErrorCode::failed, "Executor threw an unknown exception");
            }
        }();
        step.elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        if (!result) {
            step.status = Status::failed;
            step.message = result.error().message;
        } else {
            step.exit_code = result->exit_code;
            if (result->cancelled) {
                step.status = Status::cancelled;
                step.message = "Child command cancelled";
            } else if (result->timed_out) {
                step.status = Status::failed;
                step.message = "Child command exceeded timeout";
            } else if (result->exit_code) {
                step.status = Status::failed;
                step.message = "Child exited with code " + std::to_string(result->exit_code);
            } else if (!command.expected_output.empty() && !result->matched_output) {
                step.status = Status::failed;
                step.message = "Device/runner did not emit the required output";
            } else
                step.status = Status::succeeded;
            if (step.status == Status::succeeded && command.phase == Phase::configure && project) {
                std::ifstream marker(project->binary_dir / "iisdkbuilder-system.txt");
                std::string system;
                std::getline(marker, system);
                if (!system.empty() && system.back() == '\r')
                    system.pop_back();
                if (system != detail::target_system(kit.kit, project->host_validation)) {
                    step.status = Status::failed;
                    step.message = "CMake did not verify the requested target system";
                }
            }
        }
        if (step.status != Status::succeeded) {
            report.status = step.status;
            report.message = step.message;
        }
        const bool success = step.status == Status::succeeded;
        report.steps.push_back(std::move(step));
        return success;
    };
    // SDK projects are independent. A failure in one project blocks its own
    // remaining commands and the application, but not other selected SDKs.
    const bool kit_ready = report.status == Status::succeeded;
    bool sdk_failed = false;
    std::string first_sdk_failure;
    for (std::size_t index = 0; index < kit.projects.size(); ++index) {
        const auto &project = kit.projects[index];
        const bool app = index + 1 == kit.projects.size();
        if (kit_ready && report.status != Status::cancelled) {
            if (stop.stop_requested()) {
                report.status = Status::cancelled;
                report.message = "Build cancelled before next project";
            } else if (sdk_failed) {
                report.status = app ? Status::failed : Status::succeeded;
                report.message = app ? first_sdk_failure : std::string{};
            }
        }
        if (report.status == Status::succeeded && !project.host_validation && detail::adapter_target(kit.kit)) {
            auto prepared = detail::prepare_adapter(project, kit.kit, request);
            if (!prepared) {
                report.status = Status::failed;
                report.message = prepared.error().message;
            }
        }
        for (const auto &command : project.commands)
            execute(command, &project);
        if (!app && report.status == Status::failed && !sdk_failed) {
            sdk_failed = true;
            first_sdk_failure = report.message;
        }
    }
    if (report.status != Status::succeeded)
        return report;
    try {
        const auto &app = kit.projects.back();
        auto executing = kit;
        std::vector<fs::path> executables;
        report.artifacts = detail::adapter_target(kit.kit) ? detail::adapter_artifacts(app, kit.kit)
                                                           : artifacts(app, request.configuration, kit.kit, executables,kit.prefix,report.static_sdk_links);
        bool package_requested=true;
        if(request.app.cache.contains("IISDKBUILDER_PACKAGE"))
            package_requested=enabled_option(request.app.cache,"IISDKBUILDER_PACKAGE");
        if(kit.kit.cache.contains("IISDKBUILDER_PACKAGE"))
            package_requested=enabled_option(kit.kit.cache,"IISDKBUILDER_PACKAGE");
        if(!report.static_sdk_links.empty()) {
            report.linkage="contains_static_sdk";
            const bool static_allowed=enabled_option(request.app.cache,"IISDKBUILDER_STATIC_NATIVE_AOT") ||
                enabled_option(request.app.cache,"IISDKBUILDER_ALLOW_STATIC_MODULES") ||
                enabled_option(kit.kit.cache,"IISDKBUILDER_ALLOW_STATIC_MODULES");
            if(package_requested && !static_allowed) {
                report.packaging="failed";
                throw std::runtime_error("An SDK archive is linked statically; build that module shared or explicitly allow static SDK modules");
            }
        }
        if (executing.kit.target.artifact.empty() && report.artifacts.size() == 1)
            executing.kit.target.artifact = report.artifacts.front();
        auto checks = detail::after_commands(request, executing, Phase::verify);
        for (const auto &command : checks)
            if (!execute(command))
                return report;
        auto target = kit.kit;
        if (target.architecture.empty() && target.platform != Platform::android && target.platform != Platform::wasm &&
            !detail::adapter_target(target)) {
            std::ifstream marker(app.binary_dir / "iisdkbuilder-system.txt");
            std::string system, processor, apple;
            std::getline(marker, system);
            std::getline(marker, processor);
            std::getline(marker, apple);
            for (auto *value : {&processor, &apple})
                if (!value->empty() && value->back() == '\r')
                    value->pop_back();
            target.architecture = apple.empty() ? processor : apple;
        }
        for (const auto &file : report.artifacts) {
            auto inspected = inspect_artifact(file, target, !checks.empty());
            if (!inspected)
                throw std::runtime_error(inspected.error().message);
            report.verified_artifacts.push_back(std::move(*inspected));
        }
        if(!package_requested) {
            report.packaging="not_requested";
        } else if (!executables.empty()) {
            const auto host=host_platform();
            const bool android=kit.kit.platform==Platform::android;
            const bool ios=kit.kit.platform==Platform::ios;
            const bool wasm=kit.kit.platform==Platform::wasm;
            const bool desktop=kit.kit.platform==Platform::macos || kit.kit.platform==Platform::windows ||
                               kit.kit.platform==Platform::linux_os;
            if ((!desktop && !android && !ios && !wasm) || detail::extended_target(kit.kit)) {
                report.packaging="unsupported_target";
                if (!detail::extended_target(kit.kit))
                    throw std::runtime_error("Automatic application packaging is not implemented for this target; set IISDKBUILDER_PACKAGE=OFF for compile-only output");
            } else if ((desktop && (!host || *host!=kit.kit.platform)) || (ios && (!host || *host!=Platform::macos))) {
                report.packaging="unsupported_cross_host";
                throw std::runtime_error("Native runtime dependency inspection requires a matching desktop host");
            } else {
                report.packaging="failed";
                auto script=app.binary_dir/"iisdkbuilder-package.cmake";
                auto written=detail::write_text(script,android?detail::package_android_script:
                    (ios?detail::package_ios_script:(wasm?detail::package_wasm_script:detail::package_runtime_script)));
                if(!written) throw std::runtime_error(written.error().message);
                std::string dirs;
                std::vector<fs::path> search_dirs;
                const auto add_dir=[&](const fs::path& directory) {
                    const auto text=path_string(directory);
                    if(text.find_first_of(";\n\r")!=std::string::npos)
                        throw std::runtime_error("Runtime search paths cannot contain CMake list separators or newlines");
                    if(!dirs.empty())dirs+=';';
                    dirs+=text;search_dirs.push_back(directory);
                };
                for(const auto& project:kit.projects) {
                    const auto prefix=kit.prefix/project.project.name;
                    add_dir(prefix/"lib");add_dir(prefix/"bin");
                }
                for(const auto& prefix:kit.kit.prefixes) {add_dir(prefix/"lib");add_dir(prefix/"bin");}
                std::string module_names;
                if(const auto configured=app.project.cache.find("IISDKBUILDER_RUNTIME_MODULES");configured!=app.project.cache.end())
                    module_names=configured->second;
                if(const auto configured=kit.kit.cache.find("IISDKBUILDER_RUNTIME_MODULES");configured!=kit.kit.cache.end())
                    module_names=configured->second;
                if(module_names.find_first_of("\n\r")!=std::string::npos)
                    throw std::runtime_error("Runtime module names cannot contain newlines");
                const auto option=[&](const std::string& key) -> std::string {
                    if(const auto found=kit.kit.cache.find(key);found!=kit.kit.cache.end())return found->second;
                    if(const auto found=request.app.cache.find(key);found!=request.app.cache.end())return found->second;
                    return {};
                };
                const auto option_path=[&](const std::string& key) -> fs::path {
                    const auto value=option(key);if(value.empty())return {};
                    auto path=detail::utf8_path(value);return path.is_absolute()?path:request.app.source/path;
                };
                fs::path android_ndk,android_sdk,android_manifest;
                if(android) {
                    android_ndk=option_path("IISDKBUILDER_ANDROID_NDK");
                    if(android_ndk.empty() && !kit.kit.toolchain.empty())
                        android_ndk=kit.kit.toolchain.parent_path().parent_path().parent_path();
                    android_sdk=option_path("IISDKBUILDER_ANDROID_SDK");
                    if(android_sdk.empty()) {
                        for(const auto* key:{"ANDROID_SDK_ROOT","ANDROID_HOME"}) {
                            const auto it=kit.kit.environment.find(key);
                            if(it!=kit.kit.environment.end() && !it->second.empty()) {android_sdk=detail::utf8_path(it->second);break;}
                            if(const auto* value=std::getenv(key);value&&*value) {android_sdk=detail::utf8_path(value);break;}
                        }
                    }
                    if(android_sdk.empty() && android_ndk.parent_path().filename()=="ndk")android_sdk=android_ndk.parent_path().parent_path();
                    android_manifest=option_path("IISDKBUILDER_ANDROID_MANIFEST");
                    if(android_manifest.empty())for(const auto& path:{request.app.source/"resources/android/AndroidManifest.xml",
                        request.app.source/"App/AndroidManifest.xml",request.app.source/"AndroidManifest.xml"})
                        if(fs::is_regular_file(path)){android_manifest=path;break;}
                    if(!fs::is_directory(android_ndk) || !fs::is_directory(android_sdk) || !fs::is_regular_file(android_manifest))
                        throw std::runtime_error("Android packaging requires an NDK, SDK and application AndroidManifest.xml");
                }
                std::set<fs::path> destinations;
                for(const auto& input:executables) {
                    const auto destination=kit.prefix.parent_path()/"packages"/input.filename();
                    if(!destinations.insert(destination).second)
                        throw std::runtime_error("App targets have colliding package names: "+path_string(destination));
                    const auto staging=fs::path(path_string(destination)+".staging");
                    const auto previous=fs::path(path_string(destination)+".previous");
                    if(fs::exists(fs::symlink_status(staging)) || fs::exists(fs::symlink_status(previous)))
                        throw std::runtime_error("An earlier package transaction needs inspection: "+path_string(destination));
                    std::vector<std::string> args{path_string(request.cmake),"-E","env","--unset=DYLD_LIBRARY_PATH", "--unset=DYLD_FALLBACK_LIBRARY_PATH",
                         "--unset=LD_LIBRARY_PATH","--",path_string(request.cmake),"-DINPUT="+path_string(input),
                         "-DDESTINATION="+path_string(destination),"-DPLATFORM="+std::string(platform_name(kit.kit.platform)),
                         "-DSEARCH_DIRS="+dirs,"-DMODULE_NAMES="+module_names};
                    if(wasm) {
                        auto main=input;main.replace_extension(".wasm");
                        if(!fs::is_regular_file(main))throw std::runtime_error("WebAssembly main module is missing: "+path_string(main));
                        std::vector<fs::path> queue{main},side_files;
                        std::vector<std::string> side_names,extra_names;
                        for(std::string_view remaining=module_names;!remaining.empty();) {
                            const auto separator=remaining.find(';');
                            const auto part=remaining.substr(0,separator);
                            if(part.empty())throw std::runtime_error("Empty WebAssembly runtime module name");
                            extra_names.emplace_back(part);
                            if(separator==std::string_view::npos)break;
                            remaining.remove_prefix(separator+1);
                        }
                        std::set<fs::path> seen;
                        std::map<std::string,fs::path> chosen;
                        for(std::size_t next=0;next<queue.size();++next) {
                            const auto current=queue[next];
                            const auto canonical=fs::canonical(current);
                            if(!seen.insert(canonical).second)continue;
                            auto dependencies=detail::wasm_needed(current);
                            if(!dependencies)throw std::runtime_error(dependencies.error().message+": "+path_string(current));
                            if(next==0)dependencies->insert(dependencies->end(),extra_names.begin(),extra_names.end());
                            for(const auto& name:*dependencies) {
                                if(name.empty() || name.find_first_of("\\:;\n\r")!=std::string::npos)
                                    throw std::runtime_error("Unsafe WebAssembly runtime module name");
                                const auto relative=detail::utf8_path(name);
                                if(relative.is_absolute() || std::ranges::any_of(relative,[](const auto& part){return part==".."||part==".";}))
                                    throw std::runtime_error("Unsafe WebAssembly runtime module path");
                                std::set<fs::path> matches;
                                const auto try_path=[&](const fs::path& path){if(fs::is_regular_file(path))matches.insert(fs::canonical(path));};
                                try_path(input.parent_path()/relative);
                                for(const auto& dir:search_dirs)try_path(dir/relative);
                                if(matches.size()!=1)throw std::runtime_error("WebAssembly side module missing or ambiguous: "+name);
                                const auto& match=*matches.begin();
                                if(const auto found=chosen.find(name);found!=chosen.end()) {
                                    if(found->second!=match)throw std::runtime_error("WebAssembly side module name collision: "+name);
                                } else {chosen.emplace(name,match);side_files.push_back(match);side_names.push_back(name);queue.push_back(match);}
                            }
                        }
                        auto cmake_list=[](const auto& values,auto convert) {
                            std::string out;for(const auto& value:values) {const auto text=convert(value);
                                if(text.find_first_of(";\n\r")!=std::string::npos)throw std::runtime_error("Invalid WebAssembly package path");
                                if(!out.empty())out+=';';out+=text;}return out;
                        };
                        args.push_back("-DMAIN_WASM="+path_string(main));
                        args.push_back("-DSIDE_FILES="+cmake_list(side_files,[](const auto& path){return path_string(path);}));
                        args.push_back("-DSIDE_NAMES="+cmake_list(side_names,[](const auto& name){return name;}));
                    }
                    if(android) {
                        args.insert(args.end(),{"-DANDROID_ABI="+kit.kit.android_abi,"-DANDROID_API="+std::to_string(kit.kit.android_api),
                            "-DANDROID_NDK="+path_string(android_ndk),"-DANDROID_SDK="+path_string(android_sdk),
                            "-DMANIFEST="+path_string(android_manifest)});
                        for(const auto& [key,arg]:std::initializer_list<std::pair<const char*,const char*>>{
                            {"IISDKBUILDER_ANDROID_ASSETS","ASSETS"},{"IISDKBUILDER_ANDROID_RES","ANDROID_RES"},
                            {"IISDKBUILDER_ANDROID_DEX","DEX"}})
                            if(auto path=option_path(key);!path.empty())args.push_back("-D"+std::string(arg)+"="+path_string(path));
                    }
                    args.insert(args.end(),{"-P",path_string(script)});
                    Command command{Phase::package,app.project.name,std::move(args),
                        kit.prefix.parent_path()/"logs/app"/(path_string(input.filename())+"-package.log"),request.timeout};
                    if(!execute(command)) {
                        if(fs::is_regular_file(staging/".iisdkbuilder-package")) {
                            std::error_code ec;fs::remove_all(staging,ec);
                        }
                        return report;
                    }
                    if(!fs::is_regular_file(destination/".iisdkbuilder-package"))
                        throw std::runtime_error("Package command did not produce an owned package: "+path_string(destination));
                    report.packages.push_back(destination);
                }
                report.packaging="succeeded";
                report.package_signing=(android||ios)?"unsigned":(kit.kit.platform==Platform::macos?"ad_hoc":"not_applicable");
            }
        } else if (detail::adapter_target(kit.kit) || detail::extended_target(kit.kit) ||
                   (kit.kit.platform!=Platform::macos && kit.kit.platform!=Platform::windows &&
                    kit.kit.platform!=Platform::linux_os)) {
            report.packaging="unsupported_target";
            if (!detail::adapter_target(kit.kit) && !detail::extended_target(kit.kit))
                throw std::runtime_error("Automatic application packaging is not implemented for this target; set IISDKBUILDER_PACKAGE=OFF for compile-only output");
        }
        const bool upload =
            request.deploy || (request.run && kit.kit.target.adapter == "arduino" && !kit.kit.target.port.empty());
        if (upload) {
            report.deployment = "failed";
            auto commands = detail::after_commands(request, executing, Phase::deploy);
            if (commands.empty())
                throw std::runtime_error("No explicit deployment contract");
            for (const auto &command : commands)
                if (!execute(command))
                    return report;
            report.deployment = "succeeded";
        }
        if (request.run) {
            report.execution_status = "failed";
            auto commands = detail::after_commands(request, executing, Phase::run);
            if (commands.empty())
                throw std::runtime_error("No explicit device/emulator execution contract");
            for (const auto &command : commands)
                if (!execute(command))
                    return report;
            report.execution_status = "succeeded";
        }
    } catch (const std::exception &e) {
        report.status = Status::failed;
        report.message = e.what();
    }
    return report;
}

} // namespace

std::string_view status_name(Status status) {
    switch (status) {
    case Status::succeeded:
        return "succeeded";
    case Status::failed:
        return "failed";
    case Status::unavailable:
        return "unavailable";
    case Status::cancelled:
        return "cancelled";
    case Status::blocked:return "blocked";
    }
    return "unknown";
}
std::string report_json(const Report& report) {
    Json::Array kits;
    for (const auto& k:report.kits) {
        Json::Array steps,paths,images,packages,static_links;
        for (const auto& step:k.steps) {
            Json::Array args;
            for (const auto& arg:step.command.arguments) args.emplace_back(arg);
            steps.emplace_back(Json::Object{{"project",step.command.project},{"phase",phase_name(step.command.phase)},
                {"status",status_name(step.status)},{"exitCode",step.exit_code},
                {"elapsedMilliseconds",static_cast<double>(step.elapsed.count())},{"message",step.message},
                {"arguments",std::move(args)},{"log",path_string(step.command.log)}});
        }
        for (const auto& artifact:k.artifacts) paths.emplace_back(path_string(artifact));
        for (const auto& package:k.packages) packages.emplace_back(path_string(package));
        for (const auto& link:k.static_sdk_links) static_links.emplace_back(link);
        for (const auto& image:k.verified_artifacts) {
            Json::Array architectures;
            for (const auto& arch:image.architectures) architectures.emplace_back(arch);
            images.emplace_back(Json::Object{{"file",path_string(image.file)},{"format",binary_format_name(image.format)},
                {"executionModel",execution_model_name(image.execution)},{"architectures",std::move(architectures)}});
        }
        kits.emplace_back(Json::Object{{"id",k.id},{"platform",platform_name(k.platform)},
            {"executionModel",execution_model_name(k.execution)},{"profile",k.profile},{"runner",k.runner},{"deployment",k.deployment},{"execution",k.execution_status},
            {"status",status_name(k.status)},{"message",k.message},{"steps",std::move(steps)},
            {"artifacts",std::move(paths)},{"verifiedArtifacts",std::move(images)},
            {"packaging",k.packaging},{"packageSigning",k.package_signing},{"packages",std::move(packages)},
            {"linkage",k.linkage},{"staticSdkLinks",std::move(static_links)}});
    }
    return detail::dump_json(Json::Object{{"schema",1},{"succeeded",report.succeeded},
        {"report",path_string(report.file)},{"kits",std::move(kits)}});
}
Builder::Builder(Executor executor):executor_(std::move(executor)) {
    if (!executor_) throw std::invalid_argument("Builder requires an executor");
}
core::Result<Report> Builder::build(const Request& request,std::stop_token stop) const {
    auto plan=make_plan(request);
    if (!plan) return std::unexpected(plan.error());
    try {
        iiFileProvider::native::createDirectories(plan->request.build_root);
        RootLock lock{plan->request.build_root/".iisdkbuilder.lock"};
        lock.owned=iiFileProvider::native::createDirectory(lock.path);
        if (!lock.owned) return core::fail(core::ErrorCode::busy,"Build root is locked by another run: "+path_string(lock.path));
        Report report{false,plan->request.build_root/"report.json",{}};
        report.kits.resize(plan->kits.size());
        ii::runtime::Runtime runtime{{.workers=std::min<std::size_t>(plan->request.parallel_kits,plan->kits.size()),.capacity=plan->kits.size()}};
        for (std::size_t i=0;i<plan->kits.size();++i) {
            const auto admitted=runtime.post([&,i] {
                try { report.kits[i]=build_kit(plan->kits[i],plan->request,executor_,stop); }
                catch (const std::exception& e) {
                    report.kits[i]={plan->kits[i].kit.id,plan->kits[i].kit.platform,Status::failed,e.what(),{},{}};
                } catch (...) {
                    report.kits[i]={plan->kits[i].kit.id,plan->kits[i].kit.platform,Status::failed,"Executor threw an unknown exception",{},{}};
                }
                report.kits[i].execution=execution_model(plan->kits[i].kit);report.kits[i].profile=plan->kits[i].kit.target.profile;report.kits[i].runner=plan->kits[i].kit.target.runner;
            });
            if (!admitted) return core::fail(core::ErrorCode::failed,"Runtime rejected a kit build");
        }
        if (!runtime.join()) return core::fail(core::ErrorCode::failed,"Could not join build runtime workers");
        report.succeeded=std::ranges::all_of(report.kits,[](const auto& k) { return k.status==Status::succeeded; });
        auto saved=detail::write_text(report.file,report_json(report));
        if (!saved) return std::unexpected(saved.error());
        return report;
    } catch (const std::exception& e) { return core::fail(core::ErrorCode::failed,e.what()); }
}
} // namespace ii::builder
