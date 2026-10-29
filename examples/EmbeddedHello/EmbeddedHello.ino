// Arduino resolves this library before scanning App.h.
// clang-format off
#include <iiSDKCore.h>
#include "App.h"
// clang-format on
static void output(const char *text) noexcept { Serial.print(text); }
HelloApp app(output);
void setup() {
    Serial.begin(115200);
    if (ii::embedded::contract_version() == 1)
        app.start(millis());
}
void loop() { app.poll(millis()); }
