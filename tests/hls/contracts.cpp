#include <iiSDKBuilder/Hls.h>
#include <type_traits>
static_assert(std::is_final_v<ii::builder::HlsRequest>);
static_assert(std::is_final_v<ii::builder::HlsReport>);
int main() {
    ii::builder::HlsRequest request;
    return request.max_unroll && request.max_nodes ? 0 : 1;
}
