#include "BinaryFixtures.h"
#include <algorithm>
#include <iiFileProviderStorage.h>
#include <iiSDKBuilder/Builder.h>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace ii::builder;
namespace fs = std::filesystem;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "case/output arguments");
        const std::string name = argv[1];
        const fs::path root = argv[2];
        fs::create_directories(root);
        if (name == "profiles") {
            for (auto id : {"automotive", "arduino", "fpga"})
                check(bool(parse_platform(id)), "embedded platform must parse");
            std::set<std::string> ids;
            const auto profiles = target_profiles();
            for (const auto &p : profiles)
                check(ids.insert(p.id).second, "profile IDs must be unique");
            for (auto id : {"automotive-linux",
                            "automotive-qnx",
                            "automotive-android",
                            "automotive-autosar-adaptive",
                            "automotive-autosar-classic",
                            "automotive-vxworks",
                            "automotive-integrity",
                            "automotive-pikeos",
                            "arduino-fqbn",
                            "fpga-vivado",
                            "fpga-quartus",
                            "fpga-lattice-radiant",
                            "fpga-lattice-diamond",
                            "fpga-microchip-libero",
                            "fpga-gowin",
                            "fpga-efinix",
                            "fpga-achronix",
                            "fpga-ice40",
                            "fpga-softcore",
                            "fpga-verilator"})
                check(ids.contains(id), "required commercial/board profile");
            Kit k{.id = "qnx", .platform = Platform::automotive};
            check(bool(apply_target_profile(k, "automotive-qnx")), "profile resolution");
            check(k.target.system == "QNX", "QNX target system");
            check(!apply_target_profile(k, "unknown"), "unknown profile rejected");
            Kit cpu{.id = "softcore", .platform = Platform::fpga};
            check(bool(apply_target_profile(cpu, "fpga-softcore")), "softcore profile");
            check(execution_model(cpu) == ExecutionModel::native, "softcore is native CPU execution");
            check(default_kits().size() == 6, "existing default kits remain compatible");
        } else if (name == "binary") {
            const auto file = root / "firmware.elf";
            Kit kit{.id = "avr", .platform = Platform::arduino, .architecture = "avr"};
            auto bytes = fixture::elf(false, false);
            fixture::word(bytes, 18, 83, 2);
            fixture::write(file, bytes);
            check(bool(inspect_artifact(file, kit)), "native AVR ELF");
            kit.architecture = "riscv32";
            check(!inspect_artifact(file, kit), "wrong MCU must be rejected");
            fixture::word(bytes, 18, 243, 2);
            fixture::write(file, bytes);
            check(bool(inspect_artifact(file, kit)), "native RISC-V ELF");
            for (auto cpu : std::vector<std::pair<unsigned, const char *>>{{44, "tricore"},
                                                                           {20, "ppc32"},
                                                                           {94, "xtensa"},
                                                                           {113, "nios2"},
                                                                           {189, "microblaze"},
                                                                           {173, "rx"},
                                                                           {87, "v850"}}) {
                fixture::word(bytes, 18, cpu.first, 2);
                fixture::write(file, bytes);
                kit.architecture = cpu.second;
                check(bool(inspect_artifact(file, kit)), "commercial native CPU ELF");
            }
            fixture::write(file, {'P', 'K', 3, 4});
            check(!inspect_artifact(file, kit), "archive cannot satisfy firmware");
            Kit hardware{.id = "hardware", .platform = Platform::fpga};
            hardware.target.adapter = "external";
            fixture::write(file, fixture::Bytes(64, 0x53));
            check(!inspect_artifact(file, hardware), "opaque image needs current vendor verification");
            check(bool(inspect_artifact(file, hardware, true)), "opaque image accepts explicit verifier receipt");
            for (auto magic : std::vector<fixture::Bytes>{{'M', 'Z', 0, 0},
                                                          {0xfe, 0xed, 0xfa, 0xcf},
                                                          {0x7f, 'E', 'L', 'F'},
                                                          {0, 'a', 's', 'm'},
                                                          {'P', 'K', 3, 4},
                                                          {'#', '!', 0, 0}}) {
                magic.resize(64);
                fixture::write(file, magic);
                check(!inspect_artifact(file, hardware, true),
                      "vendor verification cannot turn a CPU image/script into FPGA configuration");
            }
        } else if (name == "plans") {
            Request r;
            r.sdk_root = fs::path(IISDK_TEST_SDK);
            r.app = {"embedded", root / "sketch", {}, {}, {}};
            r.build_root = root / "build";
            fs::create_directories(r.app.source);
            fixture::write(r.app.source / "sketch.ino",
                           {'v', 'o', 'i', 'd', ' ', 's', 'e', 't', 'u', 'p', '(', ')', '{', '}'});
            Kit a{.id = "uno", .platform = Platform::arduino, .architecture = "avr"};
            a.target.profile = "arduino-fqbn";
            a.target.tool = root / "missing-arduino-cli";
            a.target.board = "arduino:avr:uno";
            r.kits = {a};
            auto plan = make_plan(r);
            check(bool(plan), "Arduino sketch does not need CMakeLists");
            check(!plan->kits.front().unavailable_reason.empty(), "absent Arduino CLI must be explicit");
            r.kits.front().target.board = "missing";
            check(!make_plan(r), "malformed board rejected");
            r.kits.front() = {.id = "fpga", .platform = Platform::fpga};
            r.kits.front().target.profile = "fpga-custom";
            check(!make_plan(r), "unknown FPGA flow cannot succeed by default");
        } else if (name == "arduino") {
            Request r;
            r.sdk_root = fs::path(IISDK_TEST_SDK);
            r.app = {"sketch", root / "Sketch", {}, {}, {}};
            r.build_root = root / "build";
            fs::create_directories(r.app.source);
            fixture::write(r.app.source / "Sketch.ino", {'/', '/', 'x'});
            Kit k{.id = "board", .platform = Platform::arduino, .architecture = "riscv32"};
            k.target.profile = "arduino-esp32";
            k.target.board = "esp32:esp32:esp32c3";
            k.target.tool = IISDK_TEST_CMAKE;
            r.kits = {k};
            auto plan = make_plan(r);
            check(bool(plan), "Arduino native plan");
            const auto binary = plan->kits.front().projects.back().binary_dir;
            bool policy_failed = false;
            Builder builder([&](const Command &command, std::stop_token) -> ii::core::Result<ProcessOutcome> {
                if (command.phase == Phase::configure) {
                    auto at = std::ranges::find(command.arguments, "-B");
                    const fs::path dir = *(at + 1);
                    fs::create_directories(dir);
                    std::ofstream f(dir / "iisdkbuilder-system.txt");
                    auto host = *host_platform();
                    f << (host == Platform::macos ? "Darwin" : host == Platform::windows ? "Windows" : "Linux") << '\n';
                }
                if (command.project == "iiSDKCore.target-policy" && policy_failed)
                    return ProcessOutcome{1, false, false};
                if (command.project == "sketch") {
                    check(fs::is_regular_file(binary / "core-library/src/iiSDKCore.h"), "Arduino resolver root header");
                    check(fs::is_regular_file(binary / "core-library/src/iiSDKCore/Embedded/Embedded.cpp"),
                          "Core is compiled by the board BSP");
                    check(std::ranges::find(command.arguments, "compiler.cpp.extra_flags=-std=gnu++23") !=
                              command.arguments.end(),
                          "C++23 enforced for target compiler");
                    const auto cpp = binary / "core-library/src/iiSDKCore/Embedded/Embedded.cpp";
                    const auto json = "[{\"directory\":\"" + binary.generic_string() + "\",\"file\":\"" +
                                      cpp.generic_string() + "\",\"arguments\":[\"" +
                                      fs::path(IISDK_TEST_CMAKE).generic_string() + "\",\"-std=gnu++23\",\"-c\",\"" +
                                      cpp.generic_string() + "\",\"-o\",\"Core.o\"]}]";
                    fixture::write(binary / "compile_commands.json", fixture::Bytes(json.begin(), json.end()));
                    auto image = fixture::elf(false, false);
                    fixture::word(image, 18, 243, 2);
                    fixture::write(binary / "Sketch.ino.elf", image);
                }
                return ProcessOutcome{0, false, false};
            });
            auto report = builder.build(r);
            check(bool(report) && report->succeeded, "Arduino Core bridge pipeline");
            policy_failed = true;
            report = builder.build(r);
            check(bool(report) && !report->succeeded, "target Core policy failure blocks Arduino success");
        } else if (name == "deployment") {
            for (const auto failure : {"bad_image", "verify", "deploy", "marker", "none"}) {
                Request r;
                r.sdk_root = fs::path(IISDK_TEST_SDK);
                r.app = {"opaque", root / "source", {}, {}, {}};
                fs::create_directories(r.app.source);
                r.build_root = root / "build" / failure;
                r.deploy = true;
                r.run = true;
                Kit k{.id = "test", .platform = Platform::fpga};
                k.target.profile = "fpga-custom";
                k.target.adapter = "external";
                k.target.system = "Generic";
                k.target.foundation = "hardware";
                k.target.artifact = "design.sof";
                k.target.commands = {{{"cmake", "-E", "echo", "build"}}};
                k.target.verify = {{{"cmake", "-E", "echo", "verify"}}};
                k.target.deploy = {{{"cmake", "-E", "echo", "deploy"}}};
                k.target.run = {{{"cmake", "-E", "echo", "run"}}};
                k.target.expected_output = "BOOT_OK";
                r.kits = {k};
                auto plan = make_plan(r);
                check(bool(plan), "external vendor plan");
                const auto artifact = plan->kits.front().projects.back().binary_dir / "design.sof";
                unsigned programmed = 0, ran = 0;
                Builder builder([&](const Command &command, std::stop_token) -> ii::core::Result<ProcessOutcome> {
                    if (command.phase == Phase::configure) {
                        auto at = std::ranges::find(command.arguments, "-B");
                        check(at != command.arguments.end(), "policy binary directory");
                        const fs::path binary = *(at + 1);
                        fs::create_directories(binary);
                        std::ofstream f(binary / "iisdkbuilder-system.txt");
                        auto host = *host_platform();
                        f << (host == Platform::macos     ? "Darwin"
                              : host == Platform::windows ? "Windows"
                                                          : "Linux")
                          << '\n';
                    }
                    if (command.phase == Phase::build && command.project == "opaque")
                        fixture::write(artifact,
                                       std::string(failure) == "bad_image" ? fixture::elf() : fixture::Bytes(64, 0x53));
                    if (command.phase == Phase::verify && std::string(failure) == "verify")
                        return ProcessOutcome{1, false, false};
                    if (command.phase == Phase::deploy) {
                        ++programmed;
                        if (std::string(failure) == "deploy")
                            return ProcessOutcome{1, false, false};
                    }
                    if (command.phase == Phase::run) {
                        ++ran;
                        return ProcessOutcome{0, false, false, std::string(failure) != "marker"};
                    }
                    return ProcessOutcome{0, false, false};
                });
                const auto result = builder.build(r);
                check(bool(result), "vendor report");
                if (std::string(failure) == "none")
                    check(result->succeeded && programmed == 1 && ran == 1 &&
                              result->kits[0].execution_status == "succeeded",
                          "complete verified pipeline");
                else {
                    check(!result->succeeded, "failed required stage must fail matrix");
                    if (std::string(failure) == "bad_image" || std::string(failure) == "verify")
                        check(programmed == 0 && ran == 0 && result->kits[0].execution_status == "blocked",
                              "invalid or unverified image must never program hardware");
                    if (std::string(failure) == "deploy")
                        check(ran == 0, "failed programmer must block execution");
                }
            }
        } else if (name == "manifest") {
            Request r;
            r.app = {"fixture", root, {}, {}, {}};
            r.sdk_root = fs::path(IISDK_TEST_SDK);
            Kit k{.id = "commercial", .platform = Platform::fpga};
            k.target.profile = "fpga-custom";
            k.target.adapter = "external";
            k.target.artifact = "image.pof";
            k.target.commands = {{{"vendor-tool", "{source}", "{build}"}}};
            k.target.verify = {{{"vendor-check", "{artifact}"}}};
            k.target.run = {{{"board-test", "{artifact}"}}};
            r.kits = {k};
            r.run = true;
            check(bool(write_manifest(r, root / "target.json")), "write embedded fields");
            auto back = read_manifest(root / "target.json");
            check(bool(back) && back->run && back->kits.front().target.verify.size() == 1 &&
                      back->kits.front().target.commands[0].arguments[1] == "{source}",
                  "embedded manifest lossless round trip");
        } else
            throw std::runtime_error("unknown case");
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
