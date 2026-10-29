#include "Json.h"
#include <bit>
#include <iiFileProviderStorage.h>
#include <iiSDKBuilder/Builder.h>
#include <iiSDKCore/Process/Process.h>
#include <stdexcept>

namespace ii::builder {
core::Result<ProcessOutcome> run_process(const Command &command, std::stop_token stop) {
    if (stop.stop_requested())
        return ProcessOutcome{-1, false, true};
    if(command.synthesis) {
        auto result=synthesize_hls(*command.synthesis,stop);
        if(!result){if(result.error().code==core::ErrorCode::cancelled)return ProcessOutcome{-1,false,true};return std::unexpected(result.error());}
        auto written=detail::write_text(command.log,hls_report_json(*result));if(!written)return std::unexpected(written.error());
        return ProcessOutcome{0,false,false};
    }
    if (command.expected_output.size() > 4096 || command.expected_output.find('\0') != command.expected_output.npos ||
        (command.stop_after_match && command.expected_output.empty()))
        return core::fail(core::ErrorCode::invalid_argument, "Invalid process output observation contract");
    if (command.arguments.empty() || command.arguments.front().empty() || command.log.empty() ||
        command.timeout.count() <= 0)
        return core::fail(core::ErrorCode::invalid_argument,
                          "Process requires an executable, log path and positive timeout");
    for (const auto &argument : command.arguments)
        if (argument.find('\0') != argument.npos)
            return core::fail(core::ErrorCode::invalid_argument, "NUL in process argument");
    if (command.log.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
        return core::fail(core::ErrorCode::invalid_argument, "NUL in process log path");
    try {
        if (!command.log.parent_path().empty())
            iiFileProvider::native::createDirectories(command.log.parent_path());
        iiFileProvider::native::OutputFile log(command.log, std::ios::binary | std::ios::trunc);
        if (!log)
            return core::fail(core::ErrorCode::failed, "Cannot open process log");
        process::Launch launch;
        launch.arguments = command.arguments;
        launch.working_directory = command.working_directory;
        launch.input = process::Input::closed;
        launch.error = process::ErrorOutput::merge_output;
        auto child = process::Process::start(launch);
        if (!child)
            return std::unexpected(child.error());
        process::CommunicationOptions options;
        std::stop_source receive_stop;
        std::stop_callback forward_stop(stop, [&] { receive_stop.request_stop(); });
        bool matched = false;
        std::string tail;
        options.stop = receive_stop.get_token();
        options.deadline = core::Deadline::after(command.timeout);
        options.output_limit = 0;
        options.error_limit = 0;
        options.receive = [&](process::Stream, core::ByteView bytes) {
            log.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!log)
                throw std::runtime_error("Cannot write process log");
            if (!command.expected_output.empty() && !matched) {
                tail.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                matched = tail.find(command.expected_output) != std::string::npos;
                if (matched && command.stop_after_match)
                    receive_stop.request_stop();
                const auto keep = command.expected_output.size() - 1;
                if (tail.size() > keep)
                    tail.erase(0, tail.size() - keep);
            }
        };
        auto result = child->communicate({}, std::move(options));
        if (!result)
            return std::unexpected(result.error());
        log.flush();
        if (!log)
            return core::fail(core::ErrorCode::failed, "Cannot flush process log");
        log.close();
        if (!log)
            return core::fail(core::ErrorCode::failed, "Cannot close process log");
        const auto &exit = result->exit;
        ProcessOutcome outcome;
        outcome.matched_output = matched;
        const bool observed = matched && command.stop_after_match && !stop.stop_requested() &&
                              (exit.reason == process::ExitReason::cancelled ||
                               (exit.reason == process::ExitReason::exited && exit.code == 0 && exit.signal == 0));
        outcome.cancelled = !observed && exit.reason == process::ExitReason::cancelled;
        outcome.timed_out = exit.reason == process::ExitReason::timed_out;
        if (exit.signal)
            outcome.exit_code = 128 + exit.signal;
        else
            outcome.exit_code = static_cast<int>(std::bit_cast<std::int32_t>(exit.code));
        // Forced termination must remain a failure even if the platform reports
        // zero while the termination request races an otherwise normal exit.
        if (observed)
            outcome.exit_code = 0;
        if ((outcome.cancelled || outcome.timed_out) && outcome.exit_code == 0)
            outcome.exit_code = -1;
        return outcome;
    } catch (...) {
        return core::fail(core::ErrorCode::failed, "Cannot execute build process or publish log",
                          std::current_exception());
    }
}
} // namespace ii::builder
