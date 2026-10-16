# Architecture and platform contract

iiSDKBuilder has one responsibility: turn an application's declared SDK graph
and target environments into a verifiable CMake build matrix.

```text
application/build-tool integration
              |
      iiSDKBuilder library + CLI
      /       |        |       \
  manifest   plan   execution   native child process
              |
           iiSDKCore
  dependency graph / Runtime / platform
              |
     C++23 and native desktop APIs
```

Core 0.13 has no storage-SDK dependency. Builder publishes manifests and reports
through its direct iiFileProvider Native dependency using atomic `File::write`.
The JSON publication contract checks full replacement and original preservation
when a parent path cannot be used. This keeps Core first in the build graph.

Public data is ordinary final value records and standard containers. `Builder`
composes an executor function; it has no inheritance hierarchy or global task
registry. Core does not reference Builder. Manifest and File API JSON are a
private replaceable boundary. The JSON reader is deliberately limited to the
data needed at these boundaries, rather than becoming a new Core serialization
API. It checks syntax, numeric ranges, duplicate keys, string escapes, Unicode
surrogates, depth and input size. Reports share that encoder.

Planning resolves sources, validates names and paths, constructs the selected
SDK graph using `ii::graph::DependencyGraph`, and emits explicit command
arguments without writing files. Native prerequisite detection is preliminary;
compiler/library compatibility is established by CMake configuration and build.
The same kit settings apply to every SDK and its consumer app.

Execution owns a root lock, stores target identity, and schedules one sequential
chain per kit through `ii::runtime::Runtime`. A configure hook verifies the
final `CMAKE_SYSTEM_NAME` and writes a fresh marker. The builder requires the
marker before proceeding. This catches a foreign-kit toolchain that accidentally
produces host binaries. CMake's client-owned File API query identifies actual
application artifact paths; absence of existing app artifacts fails the kit.
Xcode's `EFFECTIVE_PLATFORM_NAME` placeholder is resolved for the kit's selected
iPhone device/simulator SDK before checking artifact existence.

Post-link inspection is mandatory for successful app builds. Only executable,
shared-library and module-library targets provide primary app images; debug
symbols and import archives are auxiliary artifacts. PE headers must identify
native CPU code and an executable section, and a CLR runtime directory is
rejected. ELF headers must identify executable/shared code, a matching CPU class
and executable load segments. Mach-O thin/universal images must contain executable
segments and the correct macOS, iOS-device or iOS-simulator deployment command.
Requested CPU families are matched against the image, including all requested
universal slices and Android's ABI. When the kit leaves its architecture empty,
the app's configured CMake processor/Apple architecture list supplies it.

This inspects image metadata, not arbitrary application behavior, every instruction
or the complete transitive dependency tree. External toolchains still own their
sysroot/library ABI correctness; device execution and deployment signing require
their separate validation.

## Native execution contract

Five OS targets produce target-specific CPU machine code and OS-loader images.
The builder does not introduce a common VM, bytecode interpreter, emulator or
interpreter launcher into those applications. iiSDKCore Runtime is a native C++
thread/task executor; it is not an application virtual machine. Native static
and shared libraries, the C/C++ standard runtime and OS frameworks are permitted.
CMake, Core's Python architecture check and test scripts run only on the build
host and are not application runtime dependencies.

WebAssembly is a virtual ISA with an engine-provided execution environment.
Its kit is explicitly `executionModel=webassembly`, never `native`, in both plans
and reports. JS/HTML glue is accepted only with a companion module carrying a
valid WebAssembly magic/version; plain JavaScript or asm.js cannot substitute
for that module. Embedded single-file WASM output is not supported by this
inspection contract. The original six-target matrix is retained, and callers
requiring only OS-native outputs select the five OS platforms explicitly.

Native execution is delegated to `ii::process::Process` from iiSDKCore 0.15.
Core owns literal argv, child-local environment/cwd, standard pipe Channels,
exit status and process-tree lifetime. Its POSIX spawn/group and Windows
CreateProcessW/Job Object implementations are shared SDK infrastructure.
Builder owns command policy and uses a synchronous streaming sink to write
merged stdout/stderr through `iiFileProvider::native::OutputFile`. Capture
limits are zero; even large build logs are drained without in-memory growth.
No process backend remains in Builder. Timeout and stop tokens terminate and
reap through Core; sink failures also retire the process before reporting error.
The host cwd and environment are preserved. See the
[Core process contract](../../iiSDKCore/docs/process.md) for platform limits,
identity protection, pipe ownership and cleanup semantics.

## Platform requirements

| Target | Build environment | Default result scope |
| --- | --- | --- |
| Windows | Native Windows compiler/SDK, or an explicit compatible cross toolchain | Native PE executable/DLL, no CLR header |
| macOS | macOS host, selected Xcode/Command Line Tools | Native Mach-O executable/dylib |
| Linux | Native Linux compiler and a libc with Core process extensions (for example glibc 2.34+), or a complete target sysroot/toolchain | Native ELF executable/shared object |
| Android | Android NDK CMake toolchain and ABI/API-specific target packages | Native ELF application/SDK code for the selected ABI |
| iOS | macOS host and Xcode iOS SDK | Native Mach-O app code; deployment signing disabled by default |
| WASM | Activated Emscripten and compatible C++23 libraries | WebAssembly module and optional web glue; separate engine execution |

Android OS entry glue/NativeActivity/JNI, APK/AAB packaging, iOS signing,
provisioning, Store uploads, browser hosting, and device deployment remain the
application's build/deployment responsibilities. A project may include packaging
in its chosen CMake targets; the builder does not manufacture those packages.
This keeps app packaging policy separate from SDK compilation.
Android application/SDK logic is compiled by the NDK; OS entry glue does not
make a JVM application an accepted replacement for native application code.

Qt Creator's documented grouping of compiler, device/target and build settings
into a Kit informed the configuration boundary. No Qt source, runtime, SDK,
tool or API is used by iiSDKBuilder. It does not invoke qmake, qt-cmake,
androiddeployqt, or macdeployqt.

## Primary references

- [Qt Creator: Add kits](https://doc.qt.io/qtcreator/creator-targets.html)
- [CMake toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html)
- [CMake File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html)
- [CMake Xcode effective platform names](https://cmake.org/cmake/help/latest/prop_gbl/XCODE_EMIT_EFFECTIVE_PLATFORM_NAME.html)
- [Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake)
- [Emscripten: Building Projects](https://emscripten.org/docs/compiling/Building-Projects.html)
- [Microsoft PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Apple Mach-O format definitions](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h)
- [ELF header specification](https://gabi.xinuos.com/elf/02-eheader.html)
- [WebAssembly execution model](https://webassembly.github.io/spec/core/intro/introduction.html)
