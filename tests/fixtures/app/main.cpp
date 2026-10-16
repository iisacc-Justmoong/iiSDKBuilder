#include <iiSDKCore/Runtime/EventLoop.h>
#include <iostream>
int main() {
    ii::runtime::EventLoop loop;
    int result = 1;
    const auto submitted = loop.post([&] { result = 0; });
    if (!submitted) return 2;
    (void)loop.poll();
    std::cout << "iiSDKBuilder native app: " << result << '\n';
    return result;
}
