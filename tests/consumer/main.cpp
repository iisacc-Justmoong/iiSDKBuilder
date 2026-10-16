#include <iiSDKBuilder/iiSDKBuilder.h>
int main() {
    const auto host=ii::builder::host_platform();
    return host && ii::builder::default_kits().size()==6 ? 0 : 1;
}
