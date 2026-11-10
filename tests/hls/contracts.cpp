#include <iiSDKBuilder/Hls.h>
#include "../../src/Build/ChildIOPolicy.h"
#include <type_traits>
static_assert(std::is_final_v<ii::builder::HlsRequest>);
static_assert(std::is_final_v<ii::builder::HlsReport>);
int main() {
    ii::builder::HlsRequest request;
    ii::process::CommunicationOptions windows, other;
    ii::builder::detail::apply_child_io_policy(windows,true);
    ii::builder::detail::apply_child_io_policy(other,false);
    return request.max_unroll && request.max_nodes && windows.polling_interval &&
        *windows.polling_interval==std::chrono::milliseconds(20) && !other.polling_interval ? 0 : 1;
}
