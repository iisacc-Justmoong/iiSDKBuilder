#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <iiSDKBuilder/Export.h>
#include <iiSDKBuilder/Hls.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace ii::builder {

enum class Platform { windows, macos, linux_os, android, ios, wasm, automotive, arduino, fpga };
enum class ExecutionModel { native, webassembly, hardware, native_simulation };
enum class BinaryFormat { pe, elf, mach_o, webassembly, bitstream, rtl };
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

struct TargetCommand final {
    std::vector<std::string> arguments;
};
struct HlsTarget final {
    HlsRequest synthesis;
    std::string implementation = "rtl", clock_port = "clk";
};
struct TargetSettings final {
    std::string profile, adapter, system, foundation;
    std::string board, part, family, top, port;
    std::filesystem::path tool, artifact;
    std::vector<std::filesystem::path> sources, constraints;
    Variables properties;
    std::vector<TargetCommand> commands, verify, deploy, run;
    std::string runner, expected_output;
    bool monitor = false;
    HlsTarget hls;
};
struct TargetProfile final {
    std::string id, name;
    Platform platform;
    std::string adapter, system, foundation;
    bool vendor_bsp = false;
};
IISDKBUILDER_EXPORT std::vector<TargetProfile> target_profiles();
IISDKBUILDER_EXPORT std::string target_profiles_json();
struct Kit;
IISDKBUILDER_EXPORT core::Result<void> apply_target_profile(Kit &, std::string_view);

// A kit is a target environment. The builder itself runs on a desktop host.
struct Kit final {
    std::string id;
    Platform platform = Platform::macos;
    std::filesystem::path toolchain;
    std::string generator;    // Empty selects Ninja, or Xcode for iOS.
    std::string architecture; // Apple architecture(s), or Visual Studio -A.
    std::string sysroot;      // Apple SDK name/path; iphoneos by default for iOS.
    unsigned android_api = 24;
    std::string android_abi = "arm64-v8a";
    Variables cache;
    Variables environment;                       // Applied to children only, through cmake -E env.
    std::vector<std::filesystem::path> prefixes; // Target packages only.
    TargetSettings target;
};
IISDKBUILDER_EXPORT std::vector<Kit> default_kits();
IISDKBUILDER_EXPORT ExecutionModel execution_model(const Kit &kit);

struct ArtifactInfo final {
    std::filesystem::path file;
    BinaryFormat format;
    ExecutionModel execution;
    std::vector<std::string> architectures;
};
// Checks machine-code image headers/CPU and rejects managed PE assemblies.
// Web targets require a real .wasm module; JS glue is not native machine code.
// This does not certify arbitrary application behavior or its entire dependency tree.
IISDKBUILDER_EXPORT core::Result<ArtifactInfo> inspect_artifact(const std::filesystem::path &file, const Kit &kit);
IISDKBUILDER_EXPORT core::Result<ArtifactInfo> inspect_artifact(const std::filesystem::path &file, const Kit &kit,
                                                                bool vendor_validated);

struct Request final {
    CMakeProject app;
    std::filesystem::path sdk_root;
    std::vector<CMakeProject> sdks; // iiSDKCore is selected automatically; SDK depends must be empty.
    std::vector<Kit> kits = default_kits();
    std::filesystem::path build_root; // Default: <app>/build/iiSDKBuilder.
    std::filesystem::path cmake = "cmake";
    std::string configuration = "Release";
    unsigned jobs = 2; // Compiler jobs per active kit.
    unsigned parallel_kits = 2;
    std::chrono::seconds timeout{1800}; // Per child command.
    bool deploy = false, run = false;
};

enum class Phase { configure, build, install, verify, deploy, run, synthesize };
IISDKBUILDER_EXPORT std::string_view phase_name(Phase phase);
struct Command final {
    Phase phase = Phase::configure;
    std::string project;
    std::vector<std::string> arguments; // Includes executable; never a shell command.
    std::filesystem::path log;
    std::chrono::seconds timeout{1800};
    std::filesystem::path working_directory{};
    std::string expected_output{};
    bool stop_after_match = false;
    std::optional<HlsRequest> synthesis{};
};
struct ProjectPlan final {
    CMakeProject project;
    std::filesystem::path binary_dir;
    std::vector<Command> commands;
    bool host_validation = false;
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

// Planning validates independent SDK selections without writing files.
IISDKBUILDER_EXPORT core::Result<Plan> make_plan(const Request &request);
IISDKBUILDER_EXPORT core::Result<Request> read_manifest(const std::filesystem::path &file);
IISDKBUILDER_EXPORT core::Result<void> write_manifest(const Request &request, const std::filesystem::path &file);
IISDKBUILDER_EXPORT std::string plan_json(const Plan &plan);

struct ProcessOutcome final {
    int exit_code = -1;
    bool timed_out = false;
    bool cancelled = false;
    bool matched_output = false;
};
IISDKBUILDER_EXPORT core::Result<ProcessOutcome> run_process(const Command &command, std::stop_token stop = {});
using Executor = std::function<core::Result<ProcessOutcome>(const Command &, std::stop_token)>;

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
    ExecutionModel execution = ExecutionModel::native;
    std::string runner{}, profile{}, deployment = "not_requested", execution_status = "not_requested";
};
struct Report final {
    bool succeeded = false; // True only if every requested kit succeeded.
    std::filesystem::path file;
    std::vector<KitReport> kits;
};
IISDKBUILDER_EXPORT std::string report_json(const Report &report);

// Each call owns one matrix run. Different kits use independent build/prefix paths.
// The injected executor is called concurrently and must be thread safe.
class IISDKBUILDER_EXPORT Builder final {
  public:
    explicit Builder(Executor executor = run_process);
    [[nodiscard]] core::Result<Report> build(const Request &request, std::stop_token stop = {}) const;

  private:
    Executor executor_;
};

} // namespace ii::builder
