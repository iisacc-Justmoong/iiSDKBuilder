#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/library.h>

int main() {
    ii::runtime::EventLoop loop;
    int value=0;
    if (!loop.post([&] { value=42; })) return 1;
    if (loop.poll()!=1 || value!=42) return 2;
    hello();
    return 0;
}
