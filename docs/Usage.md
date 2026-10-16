# Usage and manifest schema 1

The CLI is `iisdk-builder`; the library target is
`iiSDKBuilder::iiSDKBuilder`, with public headers under `<iiSDKBuilder/...>`.
The build host must be Windows, macOS, or Linux. The six target platforms are
independent entries in a single matrix.

Five OS targets compile to native machine code. Build-host tooling, including
CMake/Python, does not become an application interpreter. WASM is a separate
web-engine target, identified as `executionModel=webassembly`; every OS target
is identified as `native`. These fields are derived from the platform and cannot
be overridden through the manifest. A strictly native matrix selects
`--platform windows,macos,linux,android,ios`.

## One command for an application

From the iiSDKBuilder checkout:

```sh
build/iisdk-builder build --app examples/Hello --sdk-root ..
```

This requests all six platforms and returns a nonzero code if any requested
kit is unavailable or fails. To build only an explicitly chosen subset:

```sh
build/iisdk-builder build --app examples/Hello --sdk-root .. \
  --platform macos,android,ios,wasm
```

The default output is `<app>/build/iiSDKBuilder`. `--build-root` can select
another directory inside `build/`. Planning creates no directories. Output
must not contain source directories, and the builder never deletes source
files or another project's existing build tree.

## Reusable project configuration

```json
{
  "schema": 1,
  "app": {
    "name": "MyApp",
    "source": "../MyApp",
    "targets": ["MyApp"],
    "cache": {"MYAPP_BUILD_EXAMPLES": "OFF"}
  },
  "sdkRoot": "../../SDK",
  "sdks": [
    {
      "name": "MyFeature",
      "source": "../../SDK/MyFeature",
      "depends": ["iiSDKCore"],
      "cache": {"MYFEATURE_BUILD_TOOLS": "OFF"}
    }
  ],
  "buildRoot": "../MyApp/build/iiSDKBuilder",
  "configuration": "Release",
  "jobs": 2,
  "parallelKits": 2,
  "timeoutSeconds": 1800
}
```

Replace the example source paths with real CMake projects. All manifest paths
are relative to the manifest file, not the process working directory.
`cmake` is resolved on PATH unless it contains a directory component. The
manifest is UTF-8 JSON, bounded to 16 MiB and 64 nested levels. Duplicate keys,
unknown schema fields, incorrect field types, and NUL strings are rejected.
Cache and environment values are strings; booleans use CMake strings such as
`"ON"` and `"OFF"`.

`app.name` and every SDK name are explicit labels, independent of directory
names. SDKs are the application's selected dependencies, not every sibling
directory on disk. `iiSDKCore` is automatically inserted from
`<sdkRoot>/iiSDKCore`. Declare each additional local SDK and its dependencies;
the builder uses iiSDKCore's dependency graph to order them and rejects unknown
dependencies and cycles. Every selected SDK is built before the application.
An explicit `iiSDKCore` entry may supply its source/cache settings but cannot
depend on higher modules. `depends` on the app validates named dependencies;
it does not prune the selected SDK set. Empty `targets` builds CMake's default
target. SDKs must provide CMake install rules because their installed packages
are used by later SDKs and the application.

Create the six-kit starter manifest without overwriting an existing file:

```sh
build/iisdk-builder init --app examples/Hello --sdk-root .. \
  --manifest build/hello.json
build/iisdk-builder doctor --manifest build/hello.json
build/iisdk-builder plan --manifest build/hello.json
build/iisdk-builder build --manifest build/hello.json --platform host
```

## Kits

Omitting `kits` requests the six default kits. Provide a `kits` array to select
specific environments or multiple architectures of one platform. Kit IDs
must be unique even when case is ignored. This example shows kit fields:

```json
{
  "id": "ios-simulator-arm64",
  "platform": "ios",
  "generator": "Xcode",
  "architecture": "arm64",
  "sysroot": "iphonesimulator",
  "cache": {"CMAKE_OSX_DEPLOYMENT_TARGET": "17.0"},
  "environment": {},
  "prefixes": []
}
```

`platform` is exactly `windows`, `macos`, `linux`, `android`, `ios`, or `wasm`.
`toolchain` names a CMake toolchain file. On a foreign desktop target it is
required. `architecture` is an Apple architecture list (for example
`arm64;x86_64`) or the Visual Studio `-A` value. Other cross architectures
belong in a toolchain file. The default generator is Ninja, or Xcode for iOS.
For native Windows, run in an initialized compiler environment or use a Visual
Studio generator and its `architecture` field. MSVC/Windows SDK discovery and
compiler initialization are handled by that environment/CMake.

Android automatically uses `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` unless
`toolchain` is provided. `androidAbi` defaults to `arm64-v8a`; supported values
are `arm64-v8a`, `armeabi-v7a`, `x86`, and `x86_64`. `androidApi` defaults to 24
and must be at least 21. Use separate kit IDs for each ABI.

WASM locates `Emscripten.cmake` through `EMSDK`, `EMSCRIPTEN`, or `emcc` on PATH.
Activate emsdk before running the builder, or provide its toolchain and child
environment explicitly. iiSDKCore uses WASM threads by default. A kit cache
entry `"IISDKCORE_WASM_THREADS": "OFF"` requests its explicit single-thread
variant. Browser execution is a separate application test.

`prefixes` contains **target** dependency prefixes. The builder supplies its
isolated SDK prefix first, unsets the inherited `CMAKE_PREFIX_PATH` for child
commands, pins the installed `iiSDKCore_DIR`, and disables CMake user/system
package registries. A cross toolchain must still configure its sysroot, native
libraries, pkg-config environment and CMake root-path policy correctly. The
builder does not download or emulate missing compilers, SDKs, or sysroots.

Kit cache values are applied before per-project cache overrides. The builder
owns the configuration, toolchain, target system, platform ABI/SDK fields,
install prefix and package prefix path. Configure these through their request
or kit fields. Existing `CMAKE_PROJECT_TOP_LEVEL_INCLUDES` scripts are preserved
and the builder appends its target verification hook. Defaults include
`BUILD_TESTING=OFF`, `IISDKCORE_BUILD_EXAMPLES=OFF`, `BUILD_SHARED_LIBS=OFF`, and
`CMAKE_INSTALL_LIBDIR=lib` for the target SDK builds. C++ module scanning is
off by default because standard NDK/Emscripten distributions can omit
`clang-scan-deps`; module-based projects can set `CMAKE_CXX_SCAN_FOR_MODULES`
to `ON` in their cache and supply the scanner.

## Results, cancellation, and reuse

Each kit runs SDK configure → build → install in dependency order, then app
configure → build. Kits run concurrently through iiSDKCore Runtime. `jobs`
limits compiler jobs per kit, so an upper bound is `jobs × parallelKits` active
compiler jobs. Commands use argument vectors and per-child environment settings.
No command shell or process-global environment mutation is used by the builder.

Outputs are organized as:

```text
build/iiSDKBuilder/
  report.json
  kits/<kit-id>/<configuration>/
    kit-identity.json
    prefix/
    sdk/<sdk-name>/build/
    app/build/
    logs/sdk/<sdk-name>/<configure|build|install>.log
    logs/app/<configure|build>.log
```

`report.json` records every kit, every planned command, its status, original
arguments, exit code, elapsed time, log path and existing app artifacts from the
CMake File API. A failed SDK blocks the remaining SDK/app stages in its kit;
independent kits continue. Missing kits remain `unavailable`, and their commands
are `blocked`. The overall `succeeded` field is true only if every requested
kit succeeds. Report writes use iiFileProvider Native atomic publication.

The report includes `executionModel` for each kit and `verifiedArtifacts` for
images that pass mandatory post-link format/CPU checks. Each verified entry
contains `file`, `format`, `executionModel` and `architectures`. PE, ELF and
Mach-O images are required by their OS targets; a script launcher, JVM/DEX
bytecode or CLR managed PE cannot satisfy this contract. Apple deployment
commands distinguish macOS, iOS devices and iOS simulators. CMake success alone
does not bypass this check. CMake's primary target filename excludes PDB/debug
data and import archives from executable-image inspection.

WASM JS/HTML output must have a separate `.wasm` companion, which is inspected
and reported as the verified image. JS/asm.js alone and embedded single-file
WASM bundles are not accepted by the current inspector.

The CLI returns 0 for success, 1 for an incomplete/failed matrix or missing
doctor prerequisites, 2 for invalid input or a fatal builder error, and 130
after an interrupt. SIGINT/SIGTERM requests cancellation; active POSIX process
groups or Windows child jobs are terminated and reaped, while pending stages
are blocked. The library accepts `std::stop_token` and can use an injected
thread-safe executor for embedding or tests.

Incremental builds reuse the same paths. A kit's platform, generator, toolchain
timestamp, architecture/SDK/ABI, selected compiler cache and explicit environment
form its stored identity. Change the kit ID or build root when these change;
the builder fails rather than silently reusing an incompatible CMake cache.
Configurations use separate directories and prefixes. CMake handles ordinary
source changes and other cache updates incrementally.

A `.iisdkbuilder.lock` directory prevents concurrent runs against the same
root and is removed after a normal return. An abruptly killed builder can
leave a stale lock. Remove that **specific lock directory** only after verifying
that no build for that root is running. No automatic cleanup/reset is performed.

## Library consumption

```cmake
find_package(iiSDKBuilder 0.1 CONFIG REQUIRED)
target_link_libraries(my_build_tool PRIVATE iiSDKBuilder::iiSDKBuilder)
```

```cpp
#include <iiSDKBuilder/Builder.h>

auto request = ii::builder::read_manifest("iiSDKBuilder.json");
if (!request) { /* report request.error() */ }
else {
    ii::builder::Builder builder;
    auto report = builder.build(*request);
    if (!report) { /* fatal builder error */ }
    else if (!report->succeeded) { /* inspect per-kit/per-step results */ }
}
```

The host tooling depends on iiSDKBuilder; target applications depend on their
SDKs, including iiSDKCore. Do not link the host process runner into mobile or
browser application binaries.

## A CMake build target

The standalone helper can be included without importing the host library into
the target application. Point `BUILDER` at an existing desktop host executable:

```cmake
include("/path/to/iiSDKBuilder/cmake/iiSDKBuilderMatrix.cmake")
iisdkbuilder_add_matrix(
    TARGET build-all-platforms
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/iiSDKBuilder.json"
    BUILDER "/path/to/iiSDKBuilder/build/iisdk-builder")
```

```sh
cmake --build build --target build-all-platforms
```

The helper is also installed beside `iiSDKBuilderConfig.cmake`. `PLATFORMS host`
or `PLATFORMS macos ios android wasm` optionally limits the target. Without
`BUILDER`, it selects an existing iiSDKBuilder CLI target or searches for the
host program on PATH without using the cross-compilation root. The custom
target is explicit and is not added to the application's default build target.
