#pragma once

#include <iiSDKBuilder/Export.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace ii::builder {

enum class Platform { windows, macos, linux_os, android, ios, wasm };
enum class ExecutionModel { native, webassembly };
enum class BinaryFormat { pe, elf, mach_o, webassembly };
IISDKBUILDER_EXPORT std::string_view platform_name(Platform platform);
IISDKBUILDER_EXPORT ExecutionModel execution_model(Platform platform);
IISDKBUILDER_EXPORT std::string_view execution_model_name(ExecutionModel model);
IISDKBUILDER_EXPORT std::string_view binary_format_name(BinaryFormat format);
IISDKBUILDER_EXPORT core::Result<Platform> parse_platform(std::string_view name);
IISDKBUILDER_EXPORT core::Result<Platform> host_platform();
using Variables = std::map<std::string, std::string>;

struct CMakeProject final {
    std::string name;
    std::filesystem::path source;
    std::vector<std::string> depends;
    Variables cache;
    std::vector<std::string> targets; // Empty builds the default target.
};

// A kit is a target environment. The builder itself runs on a desktop host.
struct Kit final {
    std::string id;
    Platform platform = Platform::macos;
    std::filesystem::path toolchain;
    std::string generator; // Empty selects Ninja, or Xcode for iOS.
    std::string architecture; // Apple architecture(s), or Visual Studio -A.
    std::string sysroot; // Apple SDK name/path; iphoneos by default for iOS.
    unsigned android_api = 24;
    std::string android_abi = "arm64-v8a";
    Variables cache;
    Variables environment; // Applied to children only, through cmake -E env.
    std::vector<std::filesystem::path> prefixes; // Target packages only.
};
IISDKBUILDER_EXPORT std::vector<Kit> default_kits();

struct ArtifactInfo final {
    std::filesystem::path file;
    BinaryFormat format;
    ExecutionModel execution;
    std::vector<std::string> architectures;
};
// Checks machine-code image headers/CPU and rejects managed PE assemblies.
// Web targets require a real .wasm module; JS glue is not native machine code.
// This does not certify arbitrary application behavior or its entire dependency tree.
IISDKBUILDER_EXPORT core::Result<ArtifactInfo> inspect_artifact(
    const std::filesystem::path& file, const Kit& kit);

struct Request final {
    CMakeProject app;
    std::filesystem::path sdk_root;
    std::vector<CMakeProject> sdks; // iiSDKCore is always inserted first.
    std::vector<Kit> kits = default_kits();
    std::filesystem::path build_root; // Default: <app>/build/iiSDKBuilder.
    std::filesystem::path cmake = "cmake";
    std::string configuration = "Release";
    unsigned jobs = 2; // Compiler jobs per active kit.
    unsigned parallel_kits = 2;
    std::chrono::seconds timeout{1800}; // Per child command.
};

enum class Phase { configure, build, install };
IISDKBUILDER_EXPORT std::string_view phase_name(Phase phase);
struct Command final {
    Phase phase = Phase::configure;
    std::string project;
    std::vector<std::string> arguments; // Includes executable; never a shell command.
    std::filesystem::path log;
    std::chrono::seconds timeout{1800};
};
struct ProjectPlan final {
    CMakeProject project;
    std::filesystem::path binary_dir;
    std::vector<Command> commands;
};
struct KitPlan final {
    Kit kit;
    std::filesystem::path prefix;
    std::string unavailable_reason; // Preliminary prerequisite check, not compilation proof.
    std::vector<ProjectPlan> projects;
};
struct Plan final {
    Request request;
    std::vector<KitPlan> kits;
};

// Planning validates inputs and orders the SDK graph without writing files.
IISDKBUILDER_EXPORT core::Result<Plan> make_plan(const Request& request);
IISDKBUILDER_EXPORT core::Result<Request> read_manifest(const std::filesystem::path& file);
IISDKBUILDER_EXPORT core::Result<void> write_manifest(const Request& request,
                                                   const std::filesystem::path& file);
IISDKBUILDER_EXPORT std::string plan_json(const Plan& plan);

struct ProcessOutcome final {
    int exit_code = -1;
    bool timed_out = false;
    bool cancelled = false;
};
IISDKBUILDER_EXPORT core::Result<ProcessOutcome> run_process(const Command& command,
                                                          std::stop_token stop = {});
using Executor = std::function<core::Result<ProcessOutcome>(const Command&, std::stop_token)>;

enum class Status { succeeded, failed, unavailable, cancelled, blocked };
IISDKBUILDER_EXPORT std::string_view status_name(Status status);
struct StepReport final {
    Command command;
    Status status = Status::blocked;
    int exit_code = -1;
    std::chrono::milliseconds elapsed{};
    std::string message;
};
struct KitReport final {
    std::string id;
    Platform platform;
    Status status = Status::blocked;
    std::string message;
    std::vector<StepReport> steps;
    std::vector<std::filesystem::path> artifacts; // Existing app File API artifacts.
    std::vector<ArtifactInfo> verified_artifacts{};
};
struct Report final {
    bool succeeded = false; // True only if every requested kit succeeded.
    std::filesystem::path file;
    std::vector<KitReport> kits;
};
IISDKBUILDER_EXPORT std::string report_json(const Report& report);

// Each call owns one matrix run. Different kits use independent build/prefix paths.
// The injected executor is called concurrently and must be thread safe.
class IISDKBUILDER_EXPORT Builder final {
public:
    explicit Builder(Executor executor = run_process);
    [[nodiscard]] core::Result<Report> build(const Request& request,
                                           std::stop_token stop = {}) const;
private:
    Executor executor_;
};

} // namespace ii::builder
