#pragma once
#include <iiSDKCore/Embedded/Embedded.h>
struct HelloApp final {
    ii::embedded::EventLoop<4> events;
    void (*write)(const char *) noexcept;
    unsigned ticks = 0, hellos = 0;
    explicit HelloApp(void (*output)(const char *) noexcept) : write(output) {}
    static void hello(void *context) noexcept {
        auto &a = *static_cast<HelloApp *>(context);
        a.write("Hello, iiSDK native AOT!\n");
        ++a.hellos;
    }
    static void tick(void *context) noexcept { ++static_cast<HelloApp *>(context)->ticks; }
    void start(uint32_t now) noexcept {
        events.post({hello, this});
        events.every(now, 10, {tick, this});
    }
    void poll(uint32_t now) noexcept { events.poll(now, 4); }
};
