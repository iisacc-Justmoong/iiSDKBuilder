# Verification

## 0.2 native resource compiler — 2026-10-16

The macOS ARM64 host regression passed 14/14 CTest entries, including the native
resource compiler and a real Core -> HelloResources build. New unit checks
cover target override/common fallback, omitted missing files, content aliases,
strict manifests/paths/budgets, source/output preservation and unchanged
timestamps. The real CMake/Ninja test removes source assets for execution,
checks changed assets and manifests, skips unchanged work and rebuilds paths
containing spaces, UTF-8, # and $. It retains per-run evidence under
build/resources/resource-integration/.

Core 0.17 Debug/static, Release/shared and ASan/UBSan regressions each passed
41/41; targeted Resources/HostLoop/Operation TSan passed 3/3. The independently
installed shared Core Runtime+Values, Contracts/text and Values consumers passed
18/18, 2/2 and 3/3. Existing iiGUI regression with Core 0.17 passed 19/19.
Counts are CTest entries, not unique features or assertion counts.

A separate copied HelloResources app builds using only relocated installed Core,
the installed resource CMake module and the installed host compiler. Application
package discovery for Builder, FileProvider and Qt is disabled; its native
runtime dependencies contain Core/libc++/libSystem, not the build tool/provider.
The app still runs with its source assets removed. That relocated consumer's
one CTest entry passed. The tool's FileProvider dependency remains host-side.

| Target | Image | Core/app Builder steps | Execution |
|---|---|---:|---|
| macOS ARM64 | Native Mach-O executable | 5/5 passed | Native resource checks passed |
| Windows x64 | Native PE executable | 5/5 passed | Explicitly deferred by user |
| Linux ARM64 / Debian 13 sysroot | Native ELF executable | 5/5 passed | Explicitly deferred by user |
| Android ARM64 / API 34 | Native ELF shared library | 5/5 passed | Not run on a device |
| iOS ARM64 | Native Mach-O bundle executable | 5/5 passed | Not signed/run on a device |
| WASM32 pthread configuration | Compiled .wasm + Emscripten glue | 5/5 passed | Actual module under Node passed; browser not run |

Every kit uses the same main.cpp and manifest. The build-host compiler selects
the kit's platform file and the target compiler builds the generated const data.
The application has no shared VM/interpreter dependency on the five OS targets;
WASM remains the explicit web execution model. Android's shared library and iOS's
compile-only bundle are not complete mobile UI hosts or deployment evidence.
No MSVC build or physical Windows/Linux mapping/paging result is asserted.

The duplicate 4 MiB payload fixture stores 8,388,608 logical bytes in 4,194,496
encoded bytes; it does not claim compression or universal size reduction.
Core's resource validation record separately preserves an initial concurrent-run
ASan timeout and its unchanged isolated/full successful reruns.

Source hashes, binary/section/import inspection, package paths, XML and the
explicit execution boundaries are aggregated in build/resources-verification.json.
Core stores the same aggregate under its build/. The SDK/Product creation audit
has zero violations/unreadable files. Earlier records below retain their original
versions/scope. Global installations and existing apps are preserved.

The tests keep simulated executor behavior, real CMake compilation, installed
package consumption, and target runtime evidence separate.

## Automated checks

`ctest --test-dir build --output-on-failure` runs these contracts:

- JSON parsing/encoding, malformed input, duplicate keys, surrogate pairs and
  resource bounds.
- Manifest field/type validation, round trips and relative path resolution.
- SDK dependency order, unknown dependencies, cycles and unsafe/colliding names.
- All six kit entries and explicit missing-toolchain results.
- Literal CMake argument vectors, per-child environment, isolated prefixes,
  target selection, reserved settings and pure planning.
- Injected child failure, blocked dependents, independent kit progress, exit-code
  persistence, root locking and cancellation before any process starts.
- Real native child execution with spaces, quotes, shell metacharacters and a
  trailing backslash; timeout, active cancellation and missing executable errors.
- File API artifact references, Xcode placeholder resolution, missing artifacts,
  incompatible kit reuse and mandatory target-system verification markers.
- PE32/PE32+, ELF32/ELF64 and Mach-O thin/universal image/CPU checks;
  CLR managed PE, scripts, JVM/DEX bytecode, truncated images, wrong platform,
  iOS device/simulator mismatch and absent WASM companion rejection.
- Successful CMake/executor results producing script launchers still fail the
  build. Native app execution removes interpreter search paths from its environment.
- Real native iiSDKCore → Addon → Hello configure/build/install/consumer flow,
  app execution, incremental rebuild, a deliberately mismatched foreign kit,
  and invocation through the CMake matrix target helper.
- Staged installation, relocation, an external CMake consumer and installed CLI
  execution with Qt5 and Qt6 package discovery disabled.

Both source-provided Core and a separately installed Core dependency are
supported. When Core is supplied from source, it is installed alongside Builder
in the staging prefix. Otherwise the relocated consumer resolves the existing
Core package through its explicit package directory.

The full native integration test requires `IISDKBUILDER_CORE_SOURCE_DIR`, since
the system under test builds Core from source for its target kit. A compiler's
availability or a generated plan does not count as a successful target build.

## Evidence boundaries

The reproducible example is `examples/Hello`. To request the real six-platform
matrix, use the CLI build command without `--platform`. Its report preserves
missing and failed platforms. Do not treat a selected subset as all-six success.

Windows and Linux host execution of the builder, and Windows Job Object behavior,
must be run on those systems before being described as verified there. Android
and iOS cross-compilation does not establish physical-device execution. A WASM
link does not establish browser execution or deployment. Code signing and app
store packages require the application's separate validation.
Image/header inspection proves the recorded format and CPU metadata, not the
absence of arbitrary interpreter code deliberately embedded by an application.
The SDK/build architecture supplies native code and introduces no common VM.
WASM engine execution is always recorded separately from native OS execution.

See the generated `build/Testing/Temporary/LastTest.log` and each matrix's
`report.json`/step logs for the current run, rather than inferring results from
this source document.

## Recorded run: 2026-10-14

The macOS arm64 Release build passed all **12/12** registered CTest cases,
including real SDK/addon/app builds, native app execution, wrong-system rejection,
the CMake matrix target and relocated package/CLI consumption. Dynamic dependency
inspection of the resulting CLI contained no Qt libraries. iiSDKCore sources were
consumed unchanged.

A separate shared-library build using the installed Core package also passed
**11/11** selected tests, including its relocated shared library, external
consumer and CLI. The real source-SDK integration case was already covered by
the full 12-case build and was not repeated in the shared configuration.

The real default six-platform request using `examples/Hello` produced:

| Kit | Result | Direct evidence |
| --- | --- | --- |
| Windows | unavailable | No Windows cross compiler/sysroot toolchain configured on this host |
| macOS | succeeded | Core and app built; Mach-O arm64; app returned 0 and printed `Hello from iiSDKCore: 42` |
| Linux | unavailable | No Linux cross compiler/sysroot toolchain configured on this host |
| Android | succeeded | Core built/installed; app linked as an ARM aarch64 ELF shared object |
| iOS | succeeded | Core built/installed; app bundle contains a Mach-O arm64 executable; no code signature |
| WASM | succeeded | Core built/installed; JS and valid WASM binary generated; Node execution returned 0 and printed `Hello from iiSDKCore: 42` |

The six-platform matrix returned **1** with `succeeded=false`, preserving both
unavailable kits. The four successful kits are compilation evidence; only the
macOS native and Node WASM examples were executed. No Android/iOS physical-device,
browser, Windows-host or Linux-host execution is claimed. Android APK/AAB and
App Store packaging were not part of this run.

Post-link checks recorded Mach-O arm64 for macOS/iOS, ELF arm64 for Android,
and a separate WebAssembly module for the web kit. The native app and builder
executed successfully with PATH empty and interpreter/library override variables
removed. WASM execution used Node explicitly and is not OS-native evidence.

Machine-readable local evidence is under `build/native-contract-verification.json`,
`build/six-platform/report.json`, `build/native-contract-tests.xml`, and
`build/shared-native-contract-tests.xml`. The verification record contains
artifact hashes, inspected image/CPU metadata and runtime output. Windows/Linux
header-format tests use artificial image fixtures and do not establish execution
on those operating systems.
# Core 0.13 foundation migration - 2026-10-16

`build/core-foundation` builds against the staged independent Core 0.13 and
iiFileProvider Native. All 12 CTests pass, including atomic JSON replacement and
failure preservation, actual Core/Addon/app compilation, and relocated package
execution. The package test passes the resolved Native dependency directory to
its independent consumer, as it already does for Core; it does not select an
older user installation by accident. This check covers the macOS native host
and does not repeat the historical cross-platform matrix below.

## Common process migration — 0.1.1 (2026-10-16)

Builder now requires Core 0.15 and delegates process ownership and standard-pipe
pumping to `ii::process::Process`. There is no platform process backend in
Builder's src/Process.cpp. Provider OutputFile owns the complete merged log and
is flushed/closed before success. CLI/package versions derive from the same
CMake project version and the installed CLI is checked for exact equality.

The macOS arm64 Release suite in build/core-foundation passed all 12 CTest
entries, including a real Core → Addon → app build/run, target image inspection,
a relocated installed package/library consumer and the installed 0.1.1 CLI.
The process contract verifies literal quoting, normal nonzero exit versus spawn
failure, timeout, stop cancellation, a merged log larger than 1 MiB and a log
creation failure. Core's native process suite independently passed 104 checks
with normal, address/undefined and thread sanitizers and installed consumers.
See the [Core contract](../../iiSDKCore/docs/process.md) and
[platform evidence](../../iiSDKCore/docs/validation.md) for native backend and
unsupported-target boundaries. These do not establish execution of this Builder
release on Windows/Linux or deployment of the generated mobile/browser apps.

Core and Builder are installed to isolated prefixes under build/ for these
checks. The workspace creation audit reports no violations and SDK declarations
remain a DAG. Build logs, XML and the aggregate evidence are recorded in
../iiSDKCore/build/p1-verification.json. Earlier platform-matrix snapshots in this
file remain historical evidence, separate from this host process migration.
