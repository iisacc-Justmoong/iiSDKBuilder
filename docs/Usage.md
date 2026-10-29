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
      "depends": [],
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

`app.name`과 각 SDK 이름은 디렉터리명과 무관한 명시적 식별자이다. 선택한 SDK만
구성하며 `iiSDKCore`는 `<sdkRoot>/iiSDKCore`에서 별도 프로젝트로 자동 선택한다.
다른 SDK는 manifest 순서로 추가하되 각 SDK의 `depends`는 비워야 한다. 다른 SDK를
선행 조건으로 선언하면 계획 단계에서 오류가 발생한다. 앱의 `depends`는 선택한
SDK 이름들을 검증하며 선택 목록 자체를 줄이지 않는다. 선택한 SDK들의 빌드·설치가
끝난 뒤 앱을 빌드하고, 순서는 SDK 사이의 의존 관계를 뜻하지 않는다. Core의 명시적
항목으로 소스·캐시 설정을 지정할 수 있다. 빈 `targets`는 CMake 기본 대상을 빌드한다.
SDK들은 앱이 독립적으로 사용할 설치 규칙을 제공해야 한다.

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

`platform`은 `windows`, `macos`, `linux`, `android`, `ios`, `wasm`, `automotive`, `arduino`, `fpga`이다. 새 장치 타겟의 BSP·합성·실행 계약은 [EmbeddedTargets.md](EmbeddedTargets.md)를 따른다.
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

`prefixes`는 **대상 플랫폼용** 외부 의존성 경로이다. 빌드 도구는 SDK 구성에 그
SDK의 개별 설치 prefix만, 앱 구성에는 선택한 각 SDK의 prefix를 제공한다. 명시적
외부 경로는 그 뒤에 추가한다. 자식 명령에서는 상속된 `CMAKE_PREFIX_PATH`,
`CMAKE_INCLUDE_PATH`, `CMAKE_LIBRARY_PATH`, `CMAKE_FRAMEWORK_PATH`,
`CMAKE_APPBUNDLE_PATH`, `CPATH`, `C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH`,
`OBJC_INCLUDE_PATH`, `LIBRARY_PATH`, `PKG_CONFIG_PATH`, `PKG_CONFIG_LIBDIR`,
`DYLD_LIBRARY_PATH`, `DYLD_FALLBACK_LIBRARY_PATH`, `DYLD_FRAMEWORK_PATH`,
`LD_LIBRARY_PATH`를 비운다. 필요한 값은 키트 `environment`에 명시하면 제거 후
다시 적용된다. `CMAKE_PREFIX_PATH`는 `environment`가 아니라 `prefixes`로 지정한다.
CMake 사용자·시스템 패키지 레지스트리와 설치 prefix 자동 검색도 비활성화한다.
`iiSDKCore_DIR`는 앱 구성에만 지정한다. 외부 경로나 시스템 위치의 패키지가
다른 SDK를 노출하지 않는지는 해당 모듈의 별도 경계 검사로 확인해야 한다.
`PATH`와 Windows의 `INCLUDE`·`LIB`는 컴파일러 및 시스템 SDK 검색을 위해 유지하므로
이 환경 정리만으로 모든 우발적 SDK 검색을 차단한다고 보장하지 않는다.
크로스 툴체인은 sysroot·네이티브 라이브러리·pkg-config 환경과 CMake 루트 경로
정책을 별도로 정확히 설정해야 한다. 누락된 컴파일러나 SDK를 다운로드·에뮬레이션하지 않는다.

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

Each kit runs independent SDK configure → build → install in selection order, then app
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
    prefix/<sdk-name>/
    sdk/<sdk-name>/isolated/build/
    app/isolated/build/
    logs/sdk/<sdk-name>/<configure|build|install>.log
    logs/app/<configure|build>.log
```

`report.json` records every kit, every planned command, its status, original
arguments, exit code, elapsed time, log path and existing app artifacts from the
CMake File API. A failed SDK blocks its remaining stages and the app, while the
other selected SDKs in that kit continue. The first SDK failure remains the kit
error and the kit fails even if later SDKs succeed. Cancellation blocks all later
stages. Independent kits continue. Missing kits remain `unavailable`, and their
commands are `blocked`. The overall `succeeded` field is true only if every
requested kit succeeds. Report writes use iiFileProvider Native atomic publication.

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
Configurations use separate directories and prefixes. The `isolated/build`
layout leaves older shared-prefix builds and caches untouched; applications
must rebuild against the newly installed SDK packages. CMake handles ordinary
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
