# Native Hello World

This C++23 console application uses iiSDKCore's EventLoop to dispatch one task,
checks its result, then calls Core's `hello()` API. Successful execution prints
exactly `Hello, World!` followed by a newline and returns zero.

The application finds only iiSDKCore 0.15 Runtime. iiSDKBuilder is the host build
tool; it builds and installs Core into its kit prefix before compiling the app.
The builder defaults to a static Core, so the resulting host executable needs
only the platform's C/C++ system runtime. No other iiSDK module is linked into
the application. The existing builder tool's file/log implementation separately
uses iiFileProvider Native.

To repeat the verified local build from the iiSDKBuilder checkout:

```sh
build/core-foundation/package/relocated/bin/iisdk-builder build \
  --manifest build/hello-world.json
```

The exact executable path and verified native image/CPU are in
`build/hello-world/report.json`. On a macOS host the executable is:

```sh
build/hello-world/kits/macos/Release/app/build/iiSDKBuilderHello
```

A fresh project can also use `build --app examples/Hello --sdk-root ..
--platform host` with the CLI and a working host toolchain/Python on PATH. Its
default build root is examples/Hello/build/iiSDKBuilder; it does not reuse the
verified local manifest's separate kit identity.

To enable this example's CTest check when building from a manifest, set
`app.cache.BUILD_TESTING` to `"ON"`, then run CTest in the app's reported build
directory. `build/hello-world.json` is the verified local manifest from the
2026-10-16 run. It selects only Core and the host kit, enables the app test, pins
the host Python used for Core's build-time architecture check and disables
Qt/FileProvider discovery for both Core and the app. Python and the build tool
are not application runtime requirements.

The local verification record is `build/hello-world-verification.json`. It
records the build report, exact output/exit status with an empty PATH, binary
format, system-library dependencies and static Core symbols. Cross-platform
source support does not establish execution on other operating systems; this
run verifies macOS arm64.
