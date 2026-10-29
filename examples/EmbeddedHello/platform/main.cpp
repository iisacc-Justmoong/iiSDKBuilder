#include "../App.h"
#if defined(IISDK_EXAMPLE_CORTEX_M3)
extern "C" void board_write(const char *) noexcept;
static void output(const char *text) noexcept { board_write(text); }
#else
#include <cstdio>
static void output(const char *text) noexcept { std::fputs(text, stdout); }
#endif
#if defined(IISDK_EXAMPLE_CORTEX_M3)
extern "C"
#endif
    int main() {
    HelloApp app(output);
    if (ii::embedded::contract_version() != 1)
        return 2;
    app.start(0);
    for (uint32_t n = 0; n <= 30; n += 10)
        app.poll(n);
    return app.hellos == 1 && app.ticks == 3 ? 0 : 1;
}
