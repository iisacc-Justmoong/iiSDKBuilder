#include "../../src/iiSDKBuilder/Json.h"
#include <algorithm>
#include <fstream>
#include <iiFileProviderStorage.h>
#include <iiSDKBuilder/iiSDKBuilder.h>
#include <iostream>
#include <stdexcept>
using namespace ii::builder;
namespace fs = std::filesystem;
static void check(bool yes, const std::string &message) {
    if (!yes)
        throw std::runtime_error(message);
}
static void write(const fs::path &p, std::string_view s) {
    iiFileProvider::native::createDirectories(p.parent_path());
    iiFileProvider::native::OutputFile f(p);
    f << s;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
static HlsRequest request(const fs::path &out, const std::string &entry) {
    HlsRequest r;
    r.compiler = IISDK_HLS_CLANG;
    r.source = fs::path(IISDK_HLS_SOURCE) / "tests/hls/kernels.cpp";
    r.entry = entry;
    r.module = "hls_" + entry;
    r.output_directory = out / entry;
    return r;
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "case and output required");
        std::string name = argv[1];
        fs::path out = argv[2];
        if (name == "synthesis") {
            for (auto entry : {"signed_mix", "widen", "early", "short_circuit", "array_sum", "local_array"}) {
                auto r = request(out, entry);
                auto result = synthesize_hls(r);
                check(bool(result), entry + std::string(": ") + (result ? "" : result.error().message));
                check(fs::is_regular_file(result->rtl) && result->nodes > 0, "real RTL emitted");
                check(result->latency_cycles == 1 && result->initiation_interval == 1, "registered handshake contract");
                if (std::string(entry) == "widen")
                    check(result->output.bits == 64, "64-bit result");
                if (std::string(entry) == "array_sum")
                    check(result->inputs.at(0).elements == 4 && result->inputs.at(0).bits == 16 &&
                              result->unrolled_iterations == 4,
                          "fixed array and loop metadata");
            }
            auto sequence = request(out, "compound_constant");
            auto sequenced = synthesize_hls(sequence);
            check(bool(sequenced), "compound assignment synthesis");
            auto flow = ii::builder::detail::read_json(sequence.output_directory / "dataflow.json");
            check(bool(flow), "compound dataflow");
            const auto result_node = flow->at("result").integer();
            check(flow->at("nodes").array().at(result_node).at("name").string() == "8",
                  "C++23 compound RHS must be evaluated before LHS read");
            auto keyword = request(out, "early");
            keyword.module = "module";
            keyword.output_directory = out / "keyword";
            auto escaped = synthesize_hls(keyword);
            check(bool(escaped), "escaped RTL module keyword");
            auto r = request(out, "early");
            r.interface = HlsInterface::combinational;
            r.output_directory = out / "combinational";
            auto result = synthesize_hls(r);
            check(bool(result) && result->latency_cycles == 0, "combinational interface");
        } else if (name == "reject") {
            for (auto [label, source] : std::vector<std::pair<std::string, std::string>>{
                     {"float", "float kernel(float a){return a+1;}"},
                     {"while", "unsigned kernel(unsigned a){while(a>1)--a;return a;}"},
                     {"dynamic-loop",
                      "unsigned kernel(unsigned n){unsigned a=0;for(unsigned i=0;i<n;++i)a+=i;return a;}"},
                     {"pointer", "unsigned kernel(unsigned* a){return *a;}"},
                     {"state", "unsigned state=0;unsigned kernel(unsigned a){state+=a;return state;}"},
                     {"static", "unsigned kernel(unsigned a){static unsigned v=0;v+=a;return v;}"},
                     {"recursive", "unsigned kernel(unsigned a){if(a)return kernel(a-1);return 0;}"},
                     {"shift", "unsigned kernel(unsigned a,unsigned b){return a<<b;}"},
                     {"pragma", "#pragma HLS pipeline\nunsigned kernel(unsigned a){return a;}"},
                     {"uninitialized", "unsigned kernel(unsigned a){unsigned b;if(a)b=1;return b;}"}}) {
                auto r = request(out, label);
                r.source = out / (label + ".cpp");
                r.entry = "kernel";
                write(r.source, source);
                auto result = synthesize_hls(r);
                check(!result, "unsupported kernel accepted: " + label);
                check(!fs::exists(r.output_directory / (r.module + ".sv")), "unsupported kernel cannot publish RTL");
            }
            auto r = request(out, "array_sum");
            r.max_unroll = 2;
            check(!synthesize_hls(r), "unroll budget enforced");
            r.max_unroll = 256;
            r.max_nodes = 2;
            check(!synthesize_hls(r), "node budget enforced");
            r.max_nodes = 100;
            r.module = "bad;module";
            check(!synthesize_hls(r), "RTL identifier injection rejected");
            std::stop_source stop;
            stop.request_stop();
            r.module = "cancelled";
            auto result = synthesize_hls(r, stop.get_token());
            check(!result && result.error().code == ii::core::ErrorCode::cancelled, "pre-cancelled synthesis");
        } else if (name == "manifest" || name == "pipeline") {
            Request r;
            r.sdk_root = fs::path(IISDK_HLS_SOURCE).parent_path();
            r.app = {"kernel", fs::path(IISDK_HLS_SOURCE) / "examples/HLSHello", {}, {}, {}};
            r.build_root = out / "build";
            Kit k{.id = "hls", .platform = Platform::fpga};
            k.target.profile = "fpga-hls";
            k.target.tool = IISDK_HLS_CMAKE;
            k.target.hls.synthesis.source = r.app.source / "crc8.cpp";
            k.target.hls.synthesis.entry = "crc8";
            k.target.hls.synthesis.module = "crc_kernel";
            k.target.hls.synthesis.compiler = IISDK_HLS_CLANG;
            r.kits = {k};
            auto plan = make_plan(r);
            check(bool(plan), "HLS project needs no CMakeLists");
            check(plan->kits[0].projects.back().commands[0].phase == Phase::synthesize &&
                      plan->kits[0].projects.back().commands[0].synthesis.has_value(),
                  "typed native HLS command");
            if (name == "manifest") {
                check(bool(write_manifest(r, out / "manifest.json")), "write HLS manifest");
                auto back = read_manifest(out / "manifest.json");
                check(bool(back) && back->kits[0].target.hls.synthesis.entry == "crc8" &&
                          back->kits[0].target.hls.synthesis.source == k.target.hls.synthesis.source,
                      "HLS roundtrip");
                auto physical = r;
                auto &pkit = physical.kits[0];
                pkit.target.hls.implementation = "ice40";
                pkit.target.part = "hx1k";
                pkit.target.family = "tq144";
                pkit.target.top = "hls_board";
                pkit.target.constraints = {r.app.source / "ice40.pcf"};
                pkit.target.sources = {r.app.source / "board.v"};
                auto physical_plan = make_plan(physical);
                check(bool(physical_plan), "HLS implementation plan");
                bool frequency = false;
                for (const auto &command : physical_plan->kits[0].projects.back().commands) {
                    auto at = std::ranges::find(command.arguments, "--freq");
                    if (at != command.arguments.end()) {
                        frequency = true;
                        check((at + 1)->find_first_of("\n\r") == std::string::npos,
                              "clock frequency must be a scalar argv without JSON newline");
                    }
                }
                check(frequency, "HLS clock must constrain place and route");
                r.kits = default_kits();
                check(bool(write_manifest(r, out / "legacy.json")), "legacy manifest");
                std::ifstream f(out / "legacy.json");
                std::string data((std::istreambuf_iterator<char>(f)), {});
                check(data.find("\"hls\"") == std::string::npos, "legacy target identities unchanged");
            } else {
                for (bool broken : {true, false}) {
                    unsigned verified = 0;
                    Builder builder([&](const Command &c, std::stop_token) -> ii::core::Result<ProcessOutcome> {
                        if (c.phase == Phase::configure) {
                            auto at = std::ranges::find(c.arguments, "-B");
                            fs::path dir = *(at + 1);
                            write(dir / "iisdkbuilder-system.txt", *host_platform() == Platform::macos     ? "Darwin\n"
                                                                   : *host_platform() == Platform::windows ? "Windows\n"
                                                                                                           : "Linux\n");
                        }
                        if (c.phase == Phase::synthesize) {
                            if (broken)
                                return ProcessOutcome{1, false, false};
                            const auto &h = *c.synthesis;
                            write(h.output_directory / (h.module + ".sv"), "module crc_kernel; endmodule\n");
                            write(h.output_directory / "hls.json",
                                  "{\"backend\":\"native-cpp23\",\"module\":\"crc_kernel\",\"rtl\":\"" +
                                      (h.output_directory / (h.module + ".sv")).generic_string() + "\"}");
                        }
                        if (c.phase == Phase::verify)
                            ++verified;
                        return ProcessOutcome{0, false, false};
                    });
                    auto result = builder.build(r);
                    check(bool(result) && result->succeeded != broken, "failed HLS must fail matrix");
                    check(verified == unsigned(!broken), "failed HLS must block RTL verification");
                    if (!broken)
                        check(result->kits[0].verified_artifacts[0].format == BinaryFormat::rtl,
                              "RTL is not a programming image");
                }
            }
        } else
            throw std::runtime_error("unknown case");
        std::cout << name << " HLS checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
