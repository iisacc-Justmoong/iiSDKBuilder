#include "HlsIR.h"
#include <fstream>
#include <iiFileProviderStorage.h>
#include <iiSDKCore/Process/Process.h>
#include <set>
#include <stdexcept>
namespace ii::builder {
namespace {
namespace fs = std::filesystem;
using detail::Json;
bool safe_text(std::string_view s) {
    return s.find('\0') == s.npos && s.find('\n') == s.npos && s.find('\r') == s.npos;
}
void publish(const fs::path &path, std::string_view value) {
    auto result = detail::write_text(path, value);
    if (!result)
        throw std::runtime_error(result.error().message);
}
Json port(const HlsPort &p) {
    return Json::Object{{"name", p.name},
                        {"cppName", p.cpp_name},
                        {"bits", p.bits},
                        {"elements", p.elements},
                        {"signed", p.signed_value}};
}
} // namespace
std::string hls_report_json(const HlsReport &r) {
    Json::Array ports;
    for (const auto &p : r.inputs)
        ports.emplace_back(port(p));
    return detail::dump_json(
        Json::Object{{"schema", 1},
                     {"backend", "native-cpp23"},
                     {"cppStandard", 23},
                     {"dataModel", "LP64"},
                     {"signedArithmetic", "wrap"},
                     {"interface", r.interface == HlsInterface::ready_valid ? "ready-valid" : "combinational"},
                     {"requestedClockPeriodPs", r.clock_period_ps},
                     {"entry", r.entry},
                     {"module", r.module},
                     {"rtl", detail::path_string(r.rtl)},
                     {"manifest", detail::path_string(r.manifest)},
                     {"diagnostics", detail::path_string(r.diagnostics)},
                     {"inputs", std::move(ports)},
                     {"output", port(r.output)},
                     {"nodes", r.nodes},
                     {"unrolledIterations", r.unrolled_iterations},
                     {"latencyCycles", r.latency_cycles},
                     {"initiationInterval", r.initiation_interval}});
}
core::Result<HlsReport> synthesize_hls(const HlsRequest &input, std::stop_token stop) {
    if (stop.stop_requested())
        return core::fail(core::ErrorCode::cancelled, "HLS synthesis cancelled");
    try {
        const auto deadline = core::Deadline::after(input.timeout);
        auto r = input;
        if (r.compiler.is_relative() && r.compiler.has_parent_path())
            r.compiler = fs::absolute(r.compiler);
        r.source = fs::absolute(r.source).lexically_normal();
        r.output_directory = fs::absolute(r.output_directory).lexically_normal();
        if (input.source.empty() || input.output_directory.empty() || r.compiler.empty() ||
            !fs::is_regular_file(r.source) || !detail::hls::identifier(r.module) || r.entry.empty() ||
            !safe_text(r.entry) || r.timeout.count() <= 0 || !r.clock_period_ps || !r.max_unroll ||
            r.max_unroll > 4096 || !r.max_nodes || r.max_nodes > 131072)
            return core::fail(core::ErrorCode::invalid_argument,
                              "Invalid HLS source, entry, module, timeout, clock or analysis budget");
        if (r.interface != HlsInterface::ready_valid && r.interface != HlsInterface::combinational)
            return core::fail(core::ErrorCode::invalid_argument, "Invalid HLS interface");
        for (const auto &path : {r.source, r.output_directory, r.compiler})
            if (!safe_text(detail::path_string(path)))
                return core::fail(core::ErrorCode::invalid_argument, "Invalid HLS path");
        for (const auto &p : r.includes)
            if (!safe_text(detail::path_string(p)))
                return core::fail(core::ErrorCode::invalid_argument, "Invalid HLS include path");
        for (const auto &[key, value] : r.definitions)
            if (!detail::hls::identifier(key) || !safe_text(value))
                return core::fail(core::ErrorCode::invalid_argument, "Invalid HLS macro definition");
        iiFileProvider::native::createDirectories(r.output_directory);
        const auto diagnostic = r.output_directory / "frontend.log";
        const auto ast_path = r.output_directory / "frontend.ast.json";
        process::Launch launch;
        launch.arguments = {detail::path_string(r.compiler),
                            "-std=c++23",
                            "--target=x86_64-unknown-none",
                            "-ffreestanding",
                            "-fsigned-char",
                            "-fwrapv",
                            "-Werror=return-type",
                            "-Werror=unknown-pragmas",
                            "-Werror=unsequenced",
                            "-ferror-limit=8",
                            "-fsyntax-only",
                            "-Xclang",
                            "-ast-dump=json"};
        for (const auto &p : r.includes)
            launch.arguments.push_back("-I" + detail::path_string(fs::absolute(p)));
        for (const auto &[key, value] : r.definitions)
            launch.arguments.push_back("-D" + key + "=" + value);
        launch.arguments.push_back(detail::path_string(r.source));
        launch.working_directory = r.source.parent_path();
        launch.input = process::Input::closed;
        for (const auto &[key, value] : r.environment)
            launch.environment[key] = value;
        for (const auto *key : {"CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "OBJC_INCLUDE_PATH"})
            launch.environment[key] = std::nullopt;
        auto child = process::Process::start(launch);
        if (!child)
            return std::unexpected(child.error());
        process::CommunicationOptions options;
        options.stop = stop;
        options.deadline = deadline;
        options.output_limit = 16 * 1024 * 1024;
        options.error_limit = 1024 * 1024;
        auto completed = child->communicate({}, std::move(options));
        if (!completed)
            return std::unexpected(completed.error());
        auto as_text = [](const core::Bytes &bytes) {
            return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        };
        const auto errors = as_text(completed->error);
        publish(diagnostic, errors);
        if (completed->exit.reason == process::ExitReason::cancelled)
            return core::fail(core::ErrorCode::cancelled, "HLS frontend cancelled");
        if (completed->exit.reason == process::ExitReason::timed_out)
            return core::fail(core::ErrorCode::failed, "HLS frontend exceeded deadline");
        if (completed->exit.reason != process::ExitReason::exited || completed->exit.code)
            return core::fail(core::ErrorCode::failed, "HLS C++ frontend failed: " + errors);
        if (completed->output_truncated || completed->error_truncated)
            return core::fail(core::ErrorCode::out_of_range,
                              "HLS frontend output exceeded bounded AST/diagnostic capacity");
        const auto source_size = fs::file_size(r.source);
        if (source_size > 2 * 1024 * 1024)
            return core::fail(core::ErrorCode::out_of_range, "HLS source exceeds 2 MiB");
        std::ifstream source_file(r.source, std::ios::binary);
        std::string source((std::istreambuf_iterator<char>(source_file)), {});
        if (!source_file.eof() && source_file.fail())
            throw std::runtime_error("Cannot read HLS source");
        const auto ast_text = as_text(completed->output);
        auto ast = detail::parse_json(ast_text);
        if (!ast)
            return core::fail(core::ErrorCode::unsupported, "Unsupported Clang AST: " + ast.error().message);
        auto circuit = detail::hls::lower(*ast, r, source, stop, deadline);
        if (stop.stop_requested())
            return core::fail(core::ErrorCode::cancelled, "HLS synthesis cancelled");
        HlsReport report;
        report.interface = r.interface;
        report.clock_period_ps = r.clock_period_ps;
        report.entry = r.entry;
        report.module = r.module;
        report.inputs = circuit.ports;
        report.output = {"result", "return", circuit.result_type.bits, 1, circuit.result_type.sign};
        report.rtl = r.output_directory / (r.module + ".sv");
        report.manifest = r.output_directory / "hls.json";
        report.diagnostics = diagnostic;
        report.nodes = unsigned(circuit.nodes.size());
        report.unrolled_iterations = circuit.iterations;
        report.latency_cycles = r.interface == HlsInterface::ready_valid ? 1 : 0;
        report.initiation_interval = r.interface == HlsInterface::ready_valid ? 1 : 0;
        const auto rtl = detail::hls::emit_rtl(circuit, r);
        if (stop.stop_requested())
            return core::fail(core::ErrorCode::cancelled, "HLS synthesis cancelled");
        if (deadline.expired())
            return core::fail(core::ErrorCode::failed, "HLS synthesis exceeded deadline");
        publish(ast_path, ast_text);
        publish(r.output_directory / "dataflow.json", detail::dump_json(detail::hls::circuit_json(circuit)));
        publish(report.rtl, rtl);
        publish(report.manifest, hls_report_json(report));
        return report;
    } catch (const std::exception &e) {
        return core::fail(stop.stop_requested() ? core::ErrorCode::cancelled : core::ErrorCode::unsupported, e.what());
    }
}
} // namespace ii::builder
