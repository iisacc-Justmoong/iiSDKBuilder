# EmbeddedHello

One C++23 application uses `iiSDKCore::Embedded` on a native CPU or an Arduino board. `App.h` owns its bounded loop and callbacks. `EmbeddedHello.ino` binds Arduino's native Serial/millis BSP. The CMake application links the installed Embedded component. The optional Cortex-M3 BSP is specifically for QEMU LM3S6965 and is enabled only by `IISDK_EXAMPLE_CORTEX_M3=ON`.

A BSP/compiler must support C++23. A successful generic CPU emulator run is reported as emulation; it does not certify an unrelated physical board. See `docs/EmbeddedTargets.md` for profiles, toolchains and explicit deployment/runner contracts.
