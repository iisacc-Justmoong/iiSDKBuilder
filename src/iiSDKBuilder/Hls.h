#pragma once
#include <chrono>
#include <filesystem>
#include <iiSDKBuilder/Export.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

namespace ii::builder {
enum class HlsInterface { ready_valid, combinational };
struct HlsRequest final {
    std::filesystem::path source, output_directory, compiler = "clang++";
    std::string entry, module = "hls_kernel";
    HlsInterface interface = HlsInterface::ready_valid;
    unsigned clock_period_ps = 10000, max_unroll = 256, max_nodes = 16384;
    std::vector<std::filesystem::path> includes;
    std::map<std::string, std::string> definitions, environment;
    std::chrono::seconds timeout{1800};
};
struct HlsPort final {
    std::string name, cpp_name;
    unsigned bits = 0, elements = 1;
    bool signed_value = false;
};
struct HlsReport final {
    std::filesystem::path rtl, manifest, diagnostics;
    std::string entry, module;
    std::vector<HlsPort> inputs;
    HlsPort output;
    unsigned nodes = 0, unrolled_iterations = 0, latency_cycles = 0, initiation_interval = 0;
    HlsInterface interface = HlsInterface::ready_valid;
    unsigned clock_period_ps = 0;
};
// C++23/LP64 integer kernels -> typed, bounded dataflow -> synthesizable RTL.
// Compiler diagnostics and unsupported constructs fail before publishing RTL.
IISDKBUILDER_EXPORT core::Result<HlsReport> synthesize_hls(const HlsRequest &, std::stop_token stop = {});
IISDKBUILDER_EXPORT std::string hls_report_json(const HlsReport &);
} // namespace ii::builder
