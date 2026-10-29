#include <iiFileProviderStorage.h>
#include <iiSDKBuilder/Builder.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include "Json.h"
#include "Targets.h"
#include <algorithm>
#include <cctype>
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
        "function(_iisdkbuilder_verify_target)\n"
        "  if(NOT CMAKE_SYSTEM_NAME STREQUAL \""+expected+"\")\n"
        "    message(FATAL_ERROR \"iiSDKBuilder expected "+expected+", got ${CMAKE_SYSTEM_NAME}; check the kit toolchain\")\n"
        "  endif()\n"
        "  file(WRITE \"${CMAKE_BINARY_DIR}/iisdkbuilder-system.txt\" \"${CMAKE_SYSTEM_NAME}\\n${CMAKE_SYSTEM_PROCESSOR}\\n${CMAKE_OSX_ARCHITECTURES}\\n\")\n"
        "endfunction()\n"
        "cmake_language(DEFER CALL _iisdkbuilder_verify_target)\n");
    if (!result) return result;
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
std::vector<fs::path> artifacts(const ProjectPlan& app,const std::string& configuration,const Kit& kit) {
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
            if (!target.has("artifacts")) continue;
            const auto primary=target.has("nameOnDisk")?
                detail::utf8_path(target.at("nameOnDisk").string()).filename():fs::path{};
            for (const auto& artifact:target.at("artifacts").array()) {
                auto p=detail::utf8_path(artifact_path(artifact.at("path").string(),kit));
                if (!primary.empty() && p.filename()!=primary) continue; // PDB/import archives are auxiliary files.
                if (p.is_relative()) p=app.binary_dir/p;
                p=p.lexically_normal();
                if (fs::is_regular_file(p)) unique.insert(std::move(p));
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
        report.artifacts = detail::adapter_target(kit.kit) ? detail::adapter_artifacts(app, kit.kit)
                                                           : artifacts(app, request.configuration, kit.kit);
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
        Json::Array steps,paths,images;
        for (const auto& step:k.steps) {
            Json::Array args;
            for (const auto& arg:step.command.arguments) args.emplace_back(arg);
            steps.emplace_back(Json::Object{{"project",step.command.project},{"phase",phase_name(step.command.phase)},
                {"status",status_name(step.status)},{"exitCode",step.exit_code},
                {"elapsedMilliseconds",static_cast<double>(step.elapsed.count())},{"message",step.message},
                {"arguments",std::move(args)},{"log",path_string(step.command.log)}});
        }
        for (const auto& artifact:k.artifacts) paths.emplace_back(path_string(artifact));
        for (const auto& image:k.verified_artifacts) {
            Json::Array architectures;
            for (const auto& arch:image.architectures) architectures.emplace_back(arch);
            images.emplace_back(Json::Object{{"file",path_string(image.file)},{"format",binary_format_name(image.format)},
                {"executionModel",execution_model_name(image.execution)},{"architectures",std::move(architectures)}});
        }
        kits.emplace_back(Json::Object{{"id",k.id},{"platform",platform_name(k.platform)},
            {"executionModel",execution_model_name(k.execution)},{"profile",k.profile},{"runner",k.runner},{"deployment",k.deployment},{"execution",k.execution_status},
            {"status",status_name(k.status)},{"message",k.message},{"steps",std::move(steps)},
            {"artifacts",std::move(paths)},{"verifiedArtifacts",std::move(images)}});
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
