#include "Targets.h"
#include "HlsIR.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iiFileProviderStorage.h>
#include <set>
#include <stdexcept>
#include <tuple>
namespace ii::builder {
std::vector<TargetProfile> target_profiles() {
    std::vector<TargetProfile> p;
    auto add = [&](std::string id, std::string name, Platform platform, std::string adapter, std::string system,
                   std::string foundation, bool bsp = true) {
        p.push_back({std::move(id), std::move(name), platform, std::move(adapter), std::move(system),
                     std::move(foundation), bsp});
    };
    for (auto [id, name, system] : std::vector<std::tuple<const char *, const char *, const char *>>{
             {"linux", "Automotive Linux / AGL / Yocto", "Linux"},
             {"qnx", "QNX Neutrino / QNX OS for Safety", "QNX"},
             {"android", "Android Automotive OS", "Android"},
             {"autosar-adaptive", "AUTOSAR Adaptive vendor BSP", "Linux"},
             {"autosar-classic", "AUTOSAR Classic vendor BSP", "Generic"},
             {"vxworks", "Wind River VxWorks", "VxWorks"},
             {"integrity", "Green Hills INTEGRITY", "Generic"},
             {"pikeos", "SYSGO PikeOS", "Generic"},
             {"lynxos", "LynxOS / LynxOS-178", "Generic"},
             {"rtems", "RTEMS", "Generic"},
             {"freertos", "FreeRTOS", "Generic"},
             {"zephyr", "Zephyr", "Generic"},
             {"threadx", "Eclipse ThreadX", "Generic"},
             {"safertos", "WITTENSTEIN SAFERTOS", "Generic"},
             {"nucleus", "Siemens Nucleus", "Generic"},
             {"embos", "SEGGER embOS", "Generic"},
             {"ucos", "Micrium uC/OS", "Generic"},
             {"osek", "OSEK / vendor RTOS", "Generic"},
             {"baremetal", "Automotive bare-metal BSP", "Generic"}})
        add(std::string("automotive-") + id, name, Platform::automotive, "cmake", system,
            std::string(id) == "linux" || std::string(id) == "android" || std::string(id) == "autosar-adaptive"
                ? "runtime"
                : "embedded");
    for (auto [id, name] : std::vector<std::pair<const char *, const char *>>{
             {"fqbn", "Any installed official or third-party Arduino FQBN"},
             {"avr", "Arduino AVR / megaAVR"},
             {"sam", "Arduino SAM / SAMD"},
             {"mbed", "Arduino Mbed OS / RP2040 / Portenta"},
             {"renesas", "Arduino Renesas UNO R4"},
             {"esp32", "Espressif ESP32 / RISC-V / Xtensa"},
             {"esp8266", "ESP8266"},
             {"stm32", "STMicroelectronics STM32"},
             {"nrf52", "Nordic nRF52"},
             {"rp2040", "Raspberry Pi RP2040 / RP2350"},
             {"teensy", "PJRC Teensy"},
             {"pic32", "Microchip PIC32 / chipKIT"},
             {"silabs", "Silicon Labs"},
             {"ti", "Texas Instruments Energia-compatible BSP"}})
        add(std::string("arduino-") + id, name, Platform::arduino, "arduino", "Generic", "embedded");
    for (auto [id, name, adapter] : std::vector<std::tuple<const char *, const char *, const char *>>{
             {"vivado", "AMD/Xilinx Vivado", "vivado"},
             {"quartus", "Altera/Intel Quartus Prime", "quartus"},
             {"ice40", "Lattice iCE40 / Yosys / nextpnr", "ice40"},
             {"verilator", "Native AOT RTL simulation / Verilator", "verilator"},
             {"lattice-radiant", "Lattice Radiant", "external"},
             {"lattice-diamond", "Lattice Diamond", "external"},
             {"ise", "AMD/Xilinx ISE legacy devices", "external"},
             {"icecube2", "Lattice iCEcube2", "external"},
             {"nanoxplore", "NanoXplore Impulse", "external"},
             {"gatemate", "Cologne Chip GateMate", "external"},
             {"raptor", "Rapid Silicon Raptor", "external"},
             {"microchip-libero", "Microchip Libero SoC / FlashPro", "external"},
             {"gowin", "GOWIN EDA", "external"},
             {"efinix", "Efinix Efinity", "external"},
             {"achronix", "Achronix ACE", "external"},
             {"quicklogic", "QuickLogic vendor flow", "external"},
             {"anlogic", "Anlogic TD / Tang Dynasty", "external"},
             {"pangomicro", "Pango Design Suite", "external"},
             {"vitis-hls", "AMD Vitis HLS", "external"},
             {"intel-hls", "Intel HLS / oneAPI FPGA", "external"},
             {"smarthls", "Microchip SmartHLS", "external"},
             {"catapult", "Siemens Catapult HLS", "external"},
             {"stratus", "Cadence Stratus HLS", "external"},
             {"synplify", "Synopsys Synplify vendor implementation", "external"},
             {"custom", "Other vendor/BSP command contract", "external"}})
        add(std::string("fpga-") + id, name, Platform::fpga, adapter, "Generic", "hardware",
            std::string(adapter) != "verilator");
    add("fpga-hls", "Native C++23 high-level synthesis", Platform::fpga, "hls", "Generic", "hardware", false);
    add("fpga-softcore", "MicroBlaze / Nios II / Nios V / RISC-V / LEON / SoC CPU BSP", Platform::fpga, "cmake",
        "Generic", "embedded");
    return p;
}
std::string target_profiles_json() {
    detail::Json::Array rows;
    for (const auto &p : target_profiles())
        rows.emplace_back(detail::Json::Object{{"id", p.id},
                                               {"name", p.name},
                                               {"platform", platform_name(p.platform)},
                                               {"adapter", p.adapter},
                                               {"system", p.system},
                                               {"foundation", p.foundation},
                                               {"vendorBsp", p.vendor_bsp},
                                               {"configurationRequired", true},
                                               {"deviceExecutionVerified", false}});
    return detail::dump_json(
        detail::Json::Object{{"schema", 1},
                             {"cppStandard", 23},
                             {"platforms", detail::Json::Array{"windows", "macos", "linux", "android", "ios", "wasm",
                                                               "automotive", "arduino", "fpga"}},
                             {"profiles", std::move(rows)}});
}
core::Result<void> apply_target_profile(Kit &kit, std::string_view id) {
    const auto profiles = target_profiles();
    const auto p = std::ranges::find(profiles, id, &TargetProfile::id);
    if (p == profiles.end() || p->platform != kit.platform)
        return core::fail(core::ErrorCode::invalid_argument,
                          "Unknown or mismatched target profile: " + std::string(id));
    auto &t = kit.target;
    t.profile = std::string(id);
    if (t.adapter.empty())
        t.adapter = p->adapter;
    if (t.system.empty())
        t.system = p->system;
    if (t.foundation.empty())
        t.foundation = p->foundation;
    return {};
}
ExecutionModel execution_model(const Kit &kit) {
    if (kit.target.adapter == "verilator")
        return ExecutionModel::native_simulation;
    if (detail::hardware_target(kit))
        return ExecutionModel::hardware;
    return kit.platform == Platform::fpga ? ExecutionModel::native : execution_model(kit.platform);
}
namespace detail {
namespace fs = std::filesystem;
namespace {
[[noreturn]] void bad(const std::string &text) { throw std::invalid_argument(text); }
std::string text(const Json &j, const char *key) { return j.has(key) ? j.at(key).string() : ""; }
void fields(const Json &j, const std::set<std::string> &allowed) {
    for (const auto &[k, v] : j.object()) {
        (void)v;
        if (!allowed.contains(k))
            bad("Unknown target field: " + k);
    }
}
bool flag(const Json &j, const char *key) {
    if (!j.has(key))
        return false;
    const auto *b = std::get_if<bool>(&j.at(key).value);
    if (!b)
        bad(std::string(key) + " must be boolean");
    return *b;
}
std::vector<TargetCommand> read_commands(const Json &j, const char *key) {
    std::vector<TargetCommand> out;
    if (j.has(key))
        for (const auto &row : j.at(key).array()) {
            TargetCommand c;
            for (const auto &arg : row.array())
                c.arguments.push_back(arg.string());
            out.push_back(std::move(c));
        }
    return out;
}
Json command_json(const std::vector<TargetCommand> &commands) {
    Json::Array rows;
    for (const auto &c : commands) {
        Json::Array args;
        for (const auto &s : c.arguments)
            args.emplace_back(s);
        rows.emplace_back(std::move(args));
    }
    return rows;
}
std::string scalar_number(double value) {
    auto result = dump_json(Json{value});
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
        result.pop_back();
    return result;
}
std::string absolute_text(const fs::path &p) { return path_string(p.empty() ? p : fs::absolute(p)); }
bool word(std::string_view s, std::string_view extra = "_.-:/,=+") {
    return !s.empty() && std::ranges::all_of(
                             s, [&](unsigned char c) { return std::isalnum(c) || extra.find(char(c)) != extra.npos; });
}
fs::path find_tool(fs::path name, const Kit &k) {
    if (name.empty())
        return {};
    if (name.has_parent_path())
        return fs::is_regular_file(name) ? fs::absolute(name) : fs::path{};
    auto var = k.environment.find("PATH");
    const auto *raw = std::getenv("PATH");
    std::string paths = var != k.environment.end() ? var->second : (raw ? raw : "");
#ifdef _WIN32
    constexpr char sep = ';';
#else
    constexpr char sep = ':';
#endif
    std::size_t pos = 0;
    do {
        const auto end = paths.find(sep, pos);
        auto p = utf8_path(paths.substr(pos, end == paths.npos ? end : end - pos)) / name;
        if (fs::is_regular_file(p))
            return fs::absolute(p);
#ifdef _WIN32
        p += L".exe";
        if (fs::is_regular_file(p))
            return fs::absolute(p);
#endif
        if (end == paths.npos)
            break;
        pos = end + 1;
    } while (pos <= paths.size());
    return {};
}
Kit hls_implementation(Kit k, const fs::path &binary) {
    const auto h = k.target.hls;
    k.target.adapter = h.implementation;
    k.target.sources.push_back(binary / "hls" / (h.synthesis.module + ".sv"));
    if (k.target.top.empty())
        k.target.top = h.synthesis.module;
    return k;
}
HlsTarget read_hls(const Json &j, const fs::path &base) {
    fields(j, {"source", "entry", "module", "compiler", "implementation", "interface", "clockPeriodPs", "maxUnroll",
               "maxNodes", "includes", "definitions", "clockPort"});
    HlsTarget h;
    auto &r = h.synthesis;
    auto resolve = [&](const std::string &value) {
        auto p = utf8_path(value);
        return p.empty() || p.is_absolute() ? p : base / p;
    };
    r.source = resolve(text(j, "source"));
    r.entry = text(j, "entry");
    if (j.has("module"))
        r.module = text(j, "module");
    if (j.has("implementation"))
        h.implementation = text(j, "implementation");
    if (j.has("clockPort"))
        h.clock_port = text(j, "clockPort");
    if (j.has("compiler")) {
        auto p = utf8_path(text(j, "compiler"));
        r.compiler = p.has_parent_path() ? resolve(text(j, "compiler")) : p;
    }
    if (j.has("interface")) {
        auto name = text(j, "interface");
        if (name == "ready-valid")
            r.interface = HlsInterface::ready_valid;
        else if (name == "combinational")
            r.interface = HlsInterface::combinational;
        else
            bad("Unknown HLS interface");
    }
    if (j.has("clockPeriodPs"))
        r.clock_period_ps = j.at("clockPeriodPs").integer();
    if (j.has("maxUnroll"))
        r.max_unroll = j.at("maxUnroll").integer();
    if (j.has("maxNodes"))
        r.max_nodes = j.at("maxNodes").integer();
    if (j.has("includes"))
        for (const auto &p : j.at("includes").array())
            r.includes.push_back(resolve(p.string()));
    if (j.has("definitions"))
        for (const auto &[key, value] : j.at("definitions").object())
            r.definitions[key] = value.string();
    return h;
}
Json hls_json(const HlsTarget &h) {
    const auto &r = h.synthesis;
    Json::Array paths;
    for (const auto &p : r.includes)
        paths.emplace_back(absolute_text(p));
    Json::Object definitions;
    for (const auto &[k, v] : r.definitions)
        definitions.emplace(k, v);
    return Json::Object{
        {"source", absolute_text(r.source)},
        {"entry", r.entry},
        {"module", r.module},
        {"compiler", r.compiler.has_parent_path() ? absolute_text(r.compiler) : path_string(r.compiler)},
        {"implementation", h.implementation},
        {"clockPort", h.clock_port},
        {"interface", r.interface == HlsInterface::ready_valid ? "ready-valid" : "combinational"},
        {"clockPeriodPs", r.clock_period_ps},
        {"maxUnroll", r.max_unroll},
        {"maxNodes", r.max_nodes},
        {"includes", std::move(paths)},
        {"definitions", std::move(definitions)}};
}
fs::path output_path(const ProjectPlan &app, const Kit &k) {
    if (!k.target.artifact.empty())
        return app.binary_dir / k.target.artifact;
    if (k.target.adapter == "hls") {
        if (k.target.hls.implementation == "rtl")
            return app.binary_dir / "hls" / (k.target.hls.synthesis.module + ".sv");
        return output_path(app, hls_implementation(k, app.binary_dir));
    }
    if (k.target.adapter == "arduino")
        return app.binary_dir / (path_string(app.project.source.filename()) + ".ino.elf");
    if (k.target.adapter == "vivado")
        return app.binary_dir / "design.bit";
    if (k.target.adapter == "quartus")
        return app.binary_dir / "output_files/design.sof";
    if (k.target.adapter == "ice40")
        return app.binary_dir / "design.bin";
#ifdef _WIN32
    if (k.target.adapter == "verilator")
        return app.binary_dir / "Vdesign.exe";
#else
    if (k.target.adapter == "verilator")
        return app.binary_dir / "Vdesign";
#endif
    if (k.target.adapter == "cmake")
        return app.binary_dir / "unresolved-artifact";
    bad("Target requires an explicit artifact path");
}
std::string expand(std::string arg, const Request &r, const KitPlan &k, const ProjectPlan &app) {
    const std::map<std::string, std::string> values{{"source", path_string(app.project.source)},
                                                    {"build", path_string(app.binary_dir)},
                                                    {"prefix", path_string(k.prefix)},
                                                    {"artifact", path_string(output_path(app, k.kit))},
                                                    {"board", k.kit.target.board},
                                                    {"part", k.kit.target.part},
                                                    {"top", k.kit.target.top},
                                                    {"port", k.kit.target.port},
                                                    {"core", path_string(r.sdk_root / "iiSDKCore")}};
    for (const auto &[key, value] : values) {
        const auto token = "{" + key + "}";
        std::size_t pos = 0;
        while ((pos = arg.find(token, pos)) != arg.npos) {
            arg.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return arg;
}
Command command(const Request &r, const KitPlan &k, const ProjectPlan &app, Phase phase, std::vector<std::string> args,
                unsigned index = 0) {
    std::vector<std::string> wrapped{path_string(r.cmake), "-E", "env", "--unset=CMAKE_PREFIX_PATH"};
    for (const auto &[name, value] : k.kit.environment)
        wrapped.push_back(name + "=" + value);
    wrapped.push_back("--");
    wrapped.insert(wrapped.end(), args.begin(), args.end());
    Command result{phase, app.project.name, std::move(wrapped),
                   app.binary_dir / "logs" / (std::string(phase_name(phase)) + "-" + std::to_string(index) + ".log"),
                   r.timeout};
    result.working_directory = app.binary_dir;
    return result;
}
std::string quote(std::string_view s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"' || c == '$' || c == '[' || c == ']')
            r += '\\';
        if (c == '\n' || c == '\r')
            bad("Newline in hardware source path");
        r += c;
    }
    return r + '"';
}
void save(const fs::path &p, const std::string &s) {
    auto result = write_text(p, s);
    if (!result)
        throw std::runtime_error(result.error().message);
}
std::string cmake_literal(const std::string &value) {
    std::string level = "=";
    while (value.find("]" + level + "]") != value.npos)
        level += '=';
    return "[" + level + "[" + value + "]" + level + "]";
}
std::vector<Command> arduino_policy(const Request &r, const KitPlan &k, const ProjectPlan &p) {
    const auto source = (p.binary_dir / "core-library/src/iiSDKCore/Embedded/Embedded.cpp").lexically_normal();
    auto database = read_json(p.binary_dir / "compile_commands.json");
    if (!database)
        bad("Arduino CLI must emit compile_commands.json for the mandatory target Core policy");
    std::vector<std::string> args;
    fs::path cwd;
    unsigned matches = 0;
    for (const auto &row : database->array()) {
        const auto directory = utf8_path(row.at("directory").string());
        const auto file = utf8_path(row.at("file").string());
        std::error_code equivalent_error;
        if (!fs::equivalent(file.is_absolute() ? file : directory / file, source, equivalent_error))
            continue;
        ++matches;
        cwd = directory;
        if (!row.has("arguments"))
            bad("Arduino compilation database must provide argv, not a shell command");
        for (const auto &a : row.at("arguments").array())
            args.push_back(a.string());
    }
    if (matches != 1 || args.empty())
        bad("Arduino BSP must compile exactly one Core Embedded translation unit");
    auto compiler = utf8_path(args.front());
    if (compiler.is_relative() && compiler.has_parent_path())
        compiler = cwd / compiler;
    compiler = find_tool(compiler, k.kit);
    if (compiler.empty())
        bad("Arduino BSP compiler is unavailable for target policy");
    std::string hook = "if(PROJECT_NAME STREQUAL iiSDKCore)\nadd_compile_options(\n";
    bool path_next = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        auto arg = args[i];
        if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ") {
            if (++i >= args.size())
                bad("Invalid Arduino compiler output option");
            continue;
        }
        if (arg == "-c" || arg == "-MMD" || arg == "-MD" || arg == "-MP")
            continue;
        if (!arg.starts_with('-') && !arg.starts_with('@') && !path_next) {
            auto file = utf8_path(arg);
            std::error_code equivalent_error;
            if (fs::equivalent(file.is_absolute() ? file : cwd / file, source, equivalent_error))
                continue;
        }
        if (arg.starts_with('@')) {
            auto file = utf8_path(arg.substr(1));
            if (file.is_relative())
                arg = "@" + path_string(cwd / file);
        }
        if (path_next) {
            auto file = utf8_path(arg);
            if (file.is_relative())
                arg = path_string(cwd / file);
            path_next = false;
        } else if (arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "-iprefix" || arg == "-include" ||
                   arg == "--sysroot")
            path_next = true;
        else if (arg.starts_with("-I") && arg.size() > 2) {
            auto file = utf8_path(arg.substr(2));
            if (file.is_relative())
                arg = "-I" + path_string(cwd / file);
        }
        if (arg.find('\0') != arg.npos || arg.find('\n') != arg.npos || arg.find('\r') != arg.npos)
            bad("Invalid Arduino compiler argument");
        hook += "    " + cmake_literal(arg) + "\n";
    }
    if (path_next)
        bad("Missing Arduino compiler include path");
    hook += ")\nendif()\n";
    const auto policy = p.binary_dir / "target-policy";
    // Response files can change independently of their argv paths. Recheck the one target TU.
    save(policy / "build/architecture/cache-.json", "{}\n");
    const auto setup = policy / "options.cmake";
    save(setup, hook);
    std::vector<std::string> configure{path_string(r.cmake),
                                       "-S",
                                       path_string(r.sdk_root / "iiSDKCore"),
                                       "-B",
                                       path_string(policy / "build"),
                                       "-G",
                                       "Ninja",
                                       "-DCMAKE_SYSTEM_NAME=Generic",
                                       "-DCMAKE_SYSTEM_PROCESSOR=" + k.kit.architecture,
                                       "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                                       "-DCMAKE_CXX_COMPILER=" + path_string(compiler),
                                       "-DCMAKE_CXX_STANDARD=23",
                                       "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
                                       "-DCMAKE_CXX_EXTENSIONS=OFF",
                                       "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF",
                                       "-DCMAKE_PROJECT_INCLUDE=" + path_string(setup),
                                       "-DIISDKCORE_EMBEDDED_ONLY=ON",
                                       "-DBUILD_TESTING=OFF"};
    auto first = command(r, k, p, Phase::verify, std::move(configure), 0);
    first.project = "iiSDKCore.target-policy";
    auto second = command(r, k, p, Phase::verify,
                          {path_string(r.cmake), "--build", path_string(policy / "build"), "--target",
                           "iiSDKCoreArchitectureCheck", "--parallel", std::to_string(r.jobs)},
                          1);
    second.project = "iiSDKCore.target-policy";
    return {std::move(first), std::move(second)};
}

} // namespace
bool extended_target(const Kit &k) {
    return k.platform == Platform::automotive || k.platform == Platform::arduino || k.platform == Platform::fpga;
}
bool adapter_target(const Kit &k) { return extended_target(k) && k.target.adapter != "cmake"; }
bool hardware_target(const Kit &k) {
    return k.platform == Platform::fpga && k.target.adapter != "cmake" && k.target.adapter != "verilator";
}
TargetSettings read_target(const Json &j, const fs::path &base) {
    fields(j, {"profile", "adapter", "system",   "foundation",     "board",       "part",       "family",   "top",
               "port",    "tool",    "artifact", "sources",        "constraints", "properties", "commands", "verify",
               "deploy",  "run",     "runner",   "expectedOutput", "monitor",     "hls"});
    TargetSettings t;
    if (j.has("hls"))
        t.hls = read_hls(j.at("hls"), base);
    t.profile = text(j, "profile");
    t.adapter = text(j, "adapter");
    t.system = text(j, "system");
    t.foundation = text(j, "foundation");
    t.board = text(j, "board");
    t.part = text(j, "part");
    t.family = text(j, "family");
    t.top = text(j, "top");
    t.port = text(j, "port");
    t.runner = text(j, "runner");
    t.expected_output = text(j, "expectedOutput");
    t.monitor = flag(j, "monitor");
    auto tool = utf8_path(text(j, "tool"));
    t.tool = tool.has_parent_path() && !tool.is_absolute() ? base / tool : tool;
    t.artifact = utf8_path(text(j, "artifact"));
    for (auto item : {std::pair{"sources", &t.sources}, std::pair{"constraints", &t.constraints}})
        if (j.has(item.first))
            for (const auto &row : j.at(item.first).array()) {
                auto p = utf8_path(row.string());
                item.second->push_back(p.is_absolute() ? p : base / p);
            }
    if (j.has("properties"))
        for (const auto &[key, value] : j.at("properties").object())
            t.properties.emplace(key, value.string());
    t.commands = read_commands(j, "commands");
    t.verify = read_commands(j, "verify");
    t.deploy = read_commands(j, "deploy");
    t.run = read_commands(j, "run");
    return t;
}
Json target_json(const TargetSettings &t) {
    Json::Array sources, constraints;
    for (const auto &p : t.sources)
        sources.emplace_back(absolute_text(p));
    for (const auto &p : t.constraints)
        constraints.emplace_back(absolute_text(p));
    Json::Object properties;
    for (const auto &[k, v] : t.properties)
        properties.emplace(k, v);
    auto result = Json::Object{{"profile", t.profile},
                               {"adapter", t.adapter},
                               {"system", t.system},
                               {"foundation", t.foundation},
                               {"board", t.board},
                               {"part", t.part},
                               {"family", t.family},
                               {"top", t.top},
                               {"port", t.port},
                               {"tool", t.tool.has_parent_path() ? absolute_text(t.tool) : path_string(t.tool)},
                               {"artifact", path_string(t.artifact)},
                               {"sources", std::move(sources)},
                               {"constraints", std::move(constraints)},
                               {"properties", std::move(properties)},
                               {"commands", command_json(t.commands)},
                               {"verify", command_json(t.verify)},
                               {"deploy", command_json(t.deploy)},
                               {"run", command_json(t.run)},
                               {"runner", t.runner},
                               {"expectedOutput", t.expected_output},
                               {"monitor", t.monitor}};
    if (t.adapter == "hls" || !t.hls.synthesis.source.empty())
        result.emplace("hls", hls_json(t.hls));
    return result;
}
void validate_target(Kit &k, const Request &r) {
    if (!extended_target(k)) {
        if (!k.target.profile.empty() || !k.target.adapter.empty())
            bad("Target settings require automotive, arduino or fpga");
        return;
    }
    auto &t = k.target;
    if (t.profile.empty())
        bad("Embedded targets require a profile or explicit custom adapter");
    auto applied = apply_target_profile(k, t.profile);
    if (!applied && (t.adapter.empty() || t.system.empty() || t.foundation.empty()))
        bad(applied.error().message + "; custom profiles require adapter/system/foundation");
    const std::set<std::string> adapters{"cmake", "arduino",   "vivado",   "quartus",
                                         "ice40", "verilator", "external", "hls"};
    if (!adapters.contains(t.adapter))
        bad("Unknown target adapter: " + t.adapter);
    if (t.foundation != "runtime" && t.foundation != "embedded" && t.foundation != "hardware")
        bad("Unknown target foundation");
    if (!word(t.system, "_-"))
        bad("Invalid CMake target system");
    if (t.adapter == "hls") {
        if (!r.sdks.empty())
            bad("HLS hardware uses Core on the build host; software SDKs require a CPU kit");
        auto &h = t.hls;
        auto &synth = h.synthesis;
        if (k.platform != Platform::fpga || t.foundation != "hardware" || !fs::is_regular_file(synth.source) ||
            synth.entry.empty() || !hls::identifier(synth.module))
            bad("HLS requires a C++ source, entry function, RTL module and hardware foundation");
        if (!synth.clock_period_ps || !synth.max_unroll || synth.max_unroll > 4096 || !synth.max_nodes ||
            synth.max_nodes > 131072)
            bad("Invalid HLS clock/unroll/node budget");
        if (h.implementation != "rtl" && h.implementation != "ice40" && h.implementation != "vivado" &&
            h.implementation != "quartus")
            bad("Unknown HLS FPGA implementation");
        if (!hls::identifier(h.clock_port))
            bad("Invalid HLS clock port");
        if (t.top.empty())
            t.top = synth.module;
        if (h.implementation == "rtl" && (r.deploy || !t.deploy.empty()))
            bad("RTL must be implemented into a device programming image before deployment");
        if (h.implementation == "rtl" && !t.artifact.empty() && t.artifact != fs::path("hls") / (synth.module + ".sv"))
            bad("HLS RTL artifact path is controlled by the synthesis module");
        if (h.implementation != "rtl" && (t.constraints.empty() || !word(t.part) || !hls::identifier(t.top)))
            bad("HLS bitstream requires a top module, device part and constraints");
        if ((h.implementation == "ice40" || h.implementation == "quartus") && t.family.empty())
            bad("HLS implementation requires a device family/package");
        for (const auto &p : t.sources)
            if (!fs::is_regular_file(p))
                bad("HLS RTL wrapper missing");
        for (const auto &p : t.constraints)
            if (!fs::is_regular_file(p))
                bad("HLS physical constraints missing");
    }
    if (t.adapter == "arduino") {
        if (k.platform != Platform::arduino || !word(t.board) || std::ranges::count(t.board, ':') < 2)
            bad("Arduino requires a valid vendor:architecture:board FQBN");
        if (k.architecture.empty())
            bad("Arduino requires the BSP CPU architecture for artifact "
                "verification");
        if (!fs::is_regular_file(r.app.source / (path_string(r.app.source.filename()) + ".ino")))
            bad("Arduino sketch must match its source directory name");
        if (!r.sdks.empty())
            bad("Arduino SDK libraries must be supplied by its BSP; the Core "
                "Embedded bridge is built explicitly");
        if (t.properties.contains("compiler.cpp.extra_flags"))
            bad("Use BSP compiler properties; compiler.cpp.extra_flags is reserved "
                "for C++23");
    }
    if (adapter_target(k) && t.adapter != "arduino" && t.adapter != "hls") {
        if (!r.sdks.empty())
            bad("Hardware adapters use the Core host foundation; CPU SDK "
                "dependencies require a CMake CPU kit");
        if (t.adapter == "external") {
            if (t.commands.empty() || t.artifact.empty() || t.verify.empty())
                bad("External hardware flow requires commands, artifact and vendor "
                    "verification");
        } else {
            if (t.sources.empty() || !word(t.top, "_"))
                bad("FPGA flow requires RTL sources and a top module");
            for (const auto &p : t.sources)
                if (!fs::is_regular_file(p))
                    bad("RTL source missing: " + path_string(p));
            if (t.adapter != "verilator" && (t.constraints.empty() || !word(t.part)))
                bad("Device bitstream requires a part and physical constraints");
            for (const auto &p : t.constraints)
                if (!fs::is_regular_file(p))
                    bad("FPGA constraints missing");
            if (t.adapter == "quartus" && t.family.empty())
                bad("Quartus requires the vendor device family");
            if (t.adapter == "ice40" && t.family.empty())
                bad("iCE40 requires the nextpnr package name");
        }
    }
    if (!t.artifact.empty() &&
        (t.artifact.is_absolute() || std::ranges::any_of(t.artifact, [](const auto &p) { return p == ".."; })))
        bad("Artifact must stay inside its kit build directory");
    for (const auto *steps : {&t.commands, &t.verify, &t.deploy, &t.run}) {
        if (steps->size() > 64)
            bad("Too many target commands");
        for (const auto &c : *steps) {
            if (c.arguments.empty() || c.arguments.size() > 256 || c.arguments.front().empty())
                bad("Target commands require bounded argv");
            for (const auto &a : c.arguments)
                if (a.find('\0') != a.npos)
                    bad("NUL in target command");
        }
    }
    if (t.expected_output.size() > 4096 || t.expected_output.find('\0') != t.expected_output.npos)
        bad("Invalid expected device output");
    if (t.monitor && t.expected_output.empty())
        bad("A finite serial monitor requires expectedOutput");
}
std::string target_preflight(Kit &k) {
    auto &t = k.target;
    if (t.adapter == "hls") {
        auto compiler = find_tool(t.hls.synthesis.compiler, k);
        if (compiler.empty())
            return "Missing Clang C++23 HLS frontend: " + path_string(t.hls.synthesis.compiler);
        t.hls.synthesis.compiler = compiler;
        if (t.hls.implementation == "rtl") {
            if (t.tool.empty())
                t.tool = "yosys";
            auto found = find_tool(t.tool, k);
            if (found.empty())
                return "Missing Yosys RTL verifier";
            t.tool = found;
            return {};
        }
        auto implementation = hls_implementation(k, {});
        auto reason = target_preflight(implementation);
        t.tool = implementation.target.tool;
        return reason;
    }
    if (t.adapter == "cmake")
        return {};
    if (t.tool.empty())
        t.tool = t.adapter == "arduino"     ? "arduino-cli"
                 : t.adapter == "quartus"   ? "quartus_sh"
                 : t.adapter == "ice40"     ? "yosys"
                 : t.adapter == "vivado"    ? "vivado"
                 : t.adapter == "verilator" ? "verilator"
                                            : "";
    if (t.adapter != "external") {
        auto executable = find_tool(t.tool, k);
        if (executable.empty())
            return "Missing " + path_string(t.tool) + "; install the vendor toolchain/BSP and license";
        t.tool = executable;
    }
    if (t.adapter == "ice40")
        for (auto name : {"nextpnr-ice40", "icepack"})
            if (find_tool(name, k).empty())
                return std::string("Missing ") + name;
    if (t.adapter == "quartus" && find_tool("quartus_cpf", k).empty())
        return "Missing quartus_cpf vendor artifact verifier";
    if (t.adapter == "external")
        for (const auto *rows : {&t.commands, &t.verify})
            for (const auto &c : *rows)
                if (c.arguments.front().find('{') == std::string::npos &&
                    find_tool(utf8_path(c.arguments.front()), k).empty())
                    return "Missing vendor command: " + c.arguments.front();
    return {};
}
std::string target_system(const Kit &k, bool host_validation) {
    if (host_validation) {
        const auto h = host_platform();
        if (!h)
            bad(h.error().message);
        return *h == Platform::macos ? "Darwin" : *h == Platform::windows ? "Windows" : "Linux";
    }
    if (extended_target(k))
        return k.target.system;
    switch (k.platform) {
    case Platform::windows:
        return "Windows";
    case Platform::macos:
        return "Darwin";
    case Platform::linux_os:
        return "Linux";
    case Platform::android:
        return "Android";
    case Platform::ios:
        return "iOS";
    case Platform::wasm:
        return "Emscripten";
    default:
        bad("Unknown target system");
    }
}
ProjectPlan adapter_plan(const Request &r, const KitPlan &k) {
    ProjectPlan p{r.app, r.build_root / "kits" / k.kit.id / r.configuration / "app" / "build", {}};
    const auto &t = k.kit.target;
    const auto tool = path_string(t.tool);
    auto add = [&](std::vector<std::string> args) {
        p.commands.push_back(command(r, k, p, Phase::build, std::move(args), unsigned(p.commands.size())));
    };
    if (t.adapter == "hls") {
        auto synthesis = t.hls.synthesis;
        synthesis.output_directory = p.binary_dir / "hls";
        synthesis.environment = k.kit.environment;
        synthesis.timeout = r.timeout;
        Command c{Phase::synthesize,
                  r.app.name,
                  {"ii::builder::synthesize_hls", path_string(synthesis.source), synthesis.entry},
                  p.binary_dir / "logs/hls.log",
                  r.timeout};
        c.synthesis = std::move(synthesis);
        p.commands.push_back(std::move(c));
        if (t.hls.implementation != "rtl") {
            auto downstream = k;
            downstream.kit = hls_implementation(k.kit, p.binary_dir);
            auto implementation = adapter_plan(r, downstream);
            for (auto &command : implementation.commands) {
                if (t.hls.implementation == "ice40" &&
                    std::ranges::find(command.arguments, "--asc") != command.arguments.end()) {
                    command.arguments.push_back("--freq");
                    command.arguments.push_back(scalar_number(1000000.0 / t.hls.synthesis.clock_period_ps));
                }
                p.commands.push_back(std::move(command));
            }
        }
    } else if (t.adapter == "arduino") {
        std::vector<std::string> args{tool,
                                      "compile",
                                      "--fqbn",
                                      t.board,
                                      "--build-path",
                                      path_string(p.binary_dir),
                                      "--library",
                                      path_string(p.binary_dir / "core-library"),
                                      "--build-property",
                                      "compiler.cpp.extra_flags=-std=gnu++23"};
        for (const auto &[key, value] : t.properties) {
            args.push_back("--build-property");
            args.push_back(key + "=" + value);
        }
        args.push_back(path_string(r.app.source));
        add(std::move(args));
    } else if (t.adapter == "vivado")
        add({tool, "-mode", "batch", "-nojournal", "-nolog", "-source", path_string(p.binary_dir / "flow.tcl")});
    else if (t.adapter == "quartus")
        add({tool, "-t", path_string(p.binary_dir / "flow.tcl")});
    else if (t.adapter == "ice40") {
        add({tool, "-q", "-s", path_string(p.binary_dir / "flow.ys")});
        add({path_string(find_tool("nextpnr-ice40", k.kit)), "--" + t.part, "--package", t.family, "--json",
             path_string(p.binary_dir / "design.json"), "--pcf", path_string(t.constraints.front()), "--asc",
             path_string(p.binary_dir / "design.asc")});
        add({path_string(find_tool("icepack", k.kit)), path_string(p.binary_dir / "design.asc"),
             path_string(output_path(p, k.kit))});
    } else if (t.adapter == "verilator") {
        std::vector<std::string> args{
            tool,       "--binary", "--timing", "--top-module", t.top, "--Mdir", path_string(p.binary_dir),
            "--prefix", "Vdesign",  "-CFLAGS",  "-std=c++23"};
        for (const auto &source : t.sources)
            args.push_back(path_string(source));
        add(std::move(args));
    } else
        for (const auto &step : t.commands) {
            auto args = step.arguments;
            for (auto &a : args)
                a = expand(std::move(a), r, k, p);
            add(std::move(args));
        }
    return p;
}
core::Result<void> prepare_adapter(const ProjectPlan &p, const Kit &k, const Request &r) {
    try {
        if (!adapter_target(k))
            return {};
        iiFileProvider::native::createDirectories(p.binary_dir);
        const auto &t = k.target;
        if (t.adapter == "hls") {
            if (t.hls.implementation == "rtl") {
                const auto rtl = p.binary_dir / "hls" / (t.hls.synthesis.module + ".sv");
                save(p.binary_dir / "hls-verify.ys", "read_verilog -sv " + quote(path_string(rtl)) +
                                                         "\nhierarchy -check -top " + t.hls.synthesis.module +
                                                         "\nproc\nopt\ncheck -assert\nstat\nwrite_json " +
                                                         quote(path_string(p.binary_dir / "hls/netlist.json")) + "\n");
            } else {
                auto downstream = hls_implementation(k, p.binary_dir);
                auto result = prepare_adapter(p, downstream, r);
                if (!result)
                    return result;
                if (t.hls.implementation == "vivado") {
                    std::ifstream file(p.binary_dir / "flow.tcl");
                    std::string script((std::istreambuf_iterator<char>(file)), {});
                    const auto insert = script.find("synth_design");
                    script.insert(insert, "create_clock -period " +
                                              scalar_number(t.hls.synthesis.clock_period_ps / 1000.0) + " [get_ports " +
                                              quote(t.hls.clock_port) + "]\n");
                    save(p.binary_dir / "flow.tcl", script);
                } else if (t.hls.implementation == "quartus") {
                    save(p.binary_dir / "hls-clock.sdc", "create_clock -name hls_clock -period " +
                                                             scalar_number(t.hls.synthesis.clock_period_ps / 1000.0) +
                                                             " [get_ports " + quote(t.hls.clock_port) + "]\n");
                    downstream.target.constraints.push_back(p.binary_dir / "hls-clock.sdc");
                    return prepare_adapter(p, downstream, r);
                }
            }
        } else if (t.adapter == "arduino") {
            const auto source = r.sdk_root / "iiSDKCore/src/Embedded";
            for (auto name : {"Embedded.h", "Embedded.cpp"}) {
                const auto dest = p.binary_dir / "core-library/src/iiSDKCore/Embedded" / name;
                iiFileProvider::native::createDirectories(dest.parent_path());
                iiFileProvider::native::copyFile(source / name, dest, fs::copy_options::overwrite_existing);
            }
            save(p.binary_dir / "core-library/src/iiSDKCore.h",
                 "#pragma once\n#include \"iiSDKCore/Embedded/Embedded.h\"\n");
            save(p.binary_dir / "core-library/library.properties",
                 "name=iiSDKCore\nversion=0.20.0\nauthor=iisacc\nmaintainer="
                 "iisacc\nsentence=Freestanding C++23 callbacks and "
                 "timers\nparagraph=Native bounded Core Embedded "
                 "foundation\ncategory=Other\narchitectures=*\nincludes=iiSDKCore."
                 "h\n");
        } else if (t.adapter == "vivado") {
            std::string s;
            for (const auto &f : t.sources)
                s += std::string(f.extension() == ".vhd" || f.extension() == ".vhdl" ? "read_vhdl "
                                                                                     : "read_verilog -sv ") +
                     quote(path_string(f)) + "\n";
            for (const auto &f : t.constraints)
                s += "read_xdc " + quote(path_string(f)) + "\n";
            s += "synth_design -top " + quote(t.top) + " -part " + quote(t.part) +
                 "\nopt_design\nplace_design\nroute_design\nreport_timing_summary "
                 "-file " +
                 quote(path_string(p.binary_dir / "timing.rpt")) + "\nwrite_bitstream -force " +
                 quote(path_string(output_path(p, k))) + "\n";
            save(p.binary_dir / "flow.tcl", s);
        } else if (t.adapter == "quartus") {
            std::string s = "package require ::quartus::flow\nproject_new design "
                            "-overwrite\nset_global_assignment -name FAMILY " +
                            quote(t.family) + "\nset_global_assignment -name DEVICE " + quote(t.part) +
                            "\nset_global_assignment -name TOP_LEVEL_ENTITY " + quote(t.top) + "\n";
            for (const auto &f : t.sources)
                s += "set_global_assignment -name " +
                     std::string(f.extension() == ".vhd" || f.extension() == ".vhdl" ? "VHDL_FILE"
                                                                                     : "SYSTEMVERILOG_FILE") +
                     " " + quote(path_string(f)) + "\n";
            for (const auto &f : t.constraints)
                s += f.extension() == ".sdc" ? "set_global_assignment -name SDC_FILE " + quote(path_string(f)) + "\n"
                                             : "source " + quote(path_string(f)) + "\n";
            s += "execute_flow -compile\nproject_close\n";
            save(p.binary_dir / "flow.tcl", s);
        } else if (t.adapter == "ice40") {
            std::string s;
            for (const auto &f : t.sources)
                s += "read_verilog -sv " + quote(path_string(f)) + "\n";
            s += "synth_ice40 -top " + t.top + " -json " + quote(path_string(p.binary_dir / "design.json")) + "\n";
            save(p.binary_dir / "flow.ys", s);
        }
        return {};
    } catch (const std::exception &e) {
        return core::fail(core::ErrorCode::failed, e.what());
    }
}
std::vector<fs::path> adapter_artifacts(const ProjectPlan &p, const Kit &k) {
    const auto path = output_path(p, k);
    if (!fs::is_regular_file(path) || fs::is_symlink(path))
        bad("Expected target artifact was not produced as a regular file: " + path_string(path));
    return {path};
}
std::vector<Command> after_commands(const Request &r, const KitPlan &k, Phase phase) {
    const auto &p = k.projects.back();
    const auto &t = k.kit.target;
    std::vector<Command> out;
    if (t.adapter == "hls") {
        if (t.hls.implementation != "rtl") {
            auto downstream = k;
            downstream.kit = hls_implementation(k.kit, p.binary_dir);
            return after_commands(r, downstream, phase);
        }
        if (phase == Phase::verify)
            out.push_back(command(r, k, p, phase,
                                  {path_string(t.tool), "-q", "-s", path_string(p.binary_dir / "hls-verify.ys")}));
    }
    if (phase == Phase::verify && t.adapter == "arduino")
        out = arduino_policy(r, k, p);
    const auto &rows = phase == Phase::verify ? t.verify : phase == Phase::deploy ? t.deploy : t.run;
    for (const auto &row : rows) {
        auto args = row.arguments;
        for (auto &a : args)
            a = expand(std::move(a), r, k, p);
        out.push_back(command(r, k, p, phase, std::move(args), unsigned(out.size())));
    }
    if (phase == Phase::verify && t.adapter == "quartus" && out.empty())
        out.push_back(command(r, k, p, phase,
                              {path_string(find_tool("quartus_cpf", k.kit)), "-c", path_string(output_path(p, k.kit)),
                               path_string(p.binary_dir / "verified.rbf")}));
    if (phase == Phase::deploy && t.adapter == "arduino" && out.empty() && !t.port.empty())
        out.push_back(command(r, k, p, phase,
                              {path_string(t.tool), "upload", "--fqbn", t.board, "--port", t.port, "--input-dir",
                               path_string(p.binary_dir)}));
    if (phase == Phase::run && t.adapter == "verilator" && out.empty())
        out.push_back(command(r, k, p, phase, {path_string(output_path(p, k.kit))}));
    if (phase == Phase::run && t.adapter == "arduino" && out.empty() && !t.port.empty() && !t.expected_output.empty()) {
        auto c = command(
            r, k, p, phase,
            {path_string(t.tool), "monitor", "--fqbn", t.board, "--port", t.port, "--config", "baudrate=115200"});
        c.stop_after_match = true;
        out.push_back(std::move(c));
    }
    if (phase == Phase::run)
        for (auto &c : out) {
            c.expected_output = t.expected_output;
            c.stop_after_match = c.stop_after_match || t.monitor;
        }
    return out;
}
} // namespace detail
} // namespace ii::builder
