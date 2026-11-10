#pragma once
#include <iiSDKCore/Process/Process.h>
#include <chrono>

namespace ii::builder::detail {
// Builder explicitly opts into the Core compatibility path only on Windows,
// whose currently adopted process pipes have no overlapped readiness event.
inline void apply_child_io_policy(process::CommunicationOptions& options, bool windows) {
    if (windows) options.polling_interval=std::chrono::milliseconds(20);
}
inline void apply_child_io_policy(process::CommunicationOptions& options) {
#if defined(_WIN32)
    apply_child_io_policy(options,true);
#else
    apply_child_io_policy(options,false);
#endif
}
}
