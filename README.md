# iiSDKBuilder

[Windows 빌드 및 실행](docs/WindowsBuildAndRun.md)

Package release: **0.2.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


iiSDKBuilder is a Qt-free C++23 library and command-line tool that builds an
application and its iiSDK dependencies through CMake for Windows, macOS, Linux,
Android, iOS, and WebAssembly in one invocation. Version 0.2 uses iiSDKCore 0.17 or newer
for platform detection, dependency ordering, parallel execution, and native
processes. The host tool uses iiFileProvider Native for manifests, reports,
and logs. Its resource compiler selects target assets, builds aligned/deduplicated
Core packs and emits native C++ const data or external mapped packs. Target apps
link only Core for those resources. See [resource compilation](docs/Resources.md)
and the common [HelloResources example](examples/HelloResources/README.md).

Windows, macOS, Linux, Android and iOS targets compile to each platform's native
CPU machine code and OS ABI. Target applications do not require iiSDKBuilder,
Python, Node, a JVM or a shared interpreter/VM runtime supplied by the builder.
iiSDKCore Runtime is a native C++ worker/task executor. Standard C/C++ runtime
libraries and native OS frameworks retain their normal role.

WebAssembly is an explicit **web** target with `executionModel=webassembly`;
it requires a WebAssembly engine and is not classified as an OS-native image.
The other five targets use `executionModel=native`. Successful builds require
post-link image/CPU inspection: PE without a CLR directory, Mach-O with the
correct Apple deployment platform, or ELF with the requested CPU/Android ABI.
Script/bytecode launchers cannot stand in for these native application artifacts.

Build and test on a desktop host with CMake 3.31+, a C++23 compiler, Ninja and
Python 3.9+ (Python is required by iiSDKCore's architecture check):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DIISDKBUILDER_CORE_SOURCE_DIR=../iiSDKCore
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

An installed `iiSDKCore` package can be used instead by omitting the source
option and setting `CMAKE_PREFIX_PATH` to its prefix. The builder runs on a
Windows, macOS or Linux host; mobile and browser targets are build outputs.

```sh
build/iisdk-builder init --app examples/Hello --sdk-root .. \
  --manifest build/hello.json
build/iisdk-builder doctor --manifest build/hello.json
build/iisdk-builder plan --manifest build/hello.json
build/iisdk-builder build --manifest build/hello.json
```

All six target platforms are requested by default. `--platform host` selects
only the current desktop platform; `--platform macos,ios,android,wasm` selects
an explicit subset. Missing prerequisites are reported as `unavailable` and
make the matrix return a nonzero exit code. A partial matrix is never reported
as complete. `doctor` only checks initial prerequisites; successful configuration,
compilation, native execution, and device/browser execution are separate evidence.
For a strictly OS-native matrix, select
`--platform windows,macos,linux,android,ios`. WASM remains available separately
to preserve the original six-target build request.

See [the manifest and CLI contract](docs/Usage.md),
[architecture and platform requirements](docs/Architecture.md), and
[verification boundaries](docs/Verification.md).

The [Hello World example](examples/Hello/README.md) is a minimal Core-only
application built through the Builder CLI. It dispatches a Core event-loop task,
then prints `Hello, World!` through Core. Builder's internal file/log dependency
does not become an application link dependency.

The native file-provider dependency is scoped to Builder and its direct test
consumers. It is not inherited by a source-built iiSDKCore. The
`iiSDKBuilder.core_foundation` regression checks the actual Core target
dependency manifest when `IISDKBUILDER_CORE_SOURCE_DIR` is enabled.

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.
