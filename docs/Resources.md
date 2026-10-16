# Native resource compilation

Builder 0.2 uses Core 0.17 to build immutable version-1 packs. The host compiler
reads/writes through iiFileProvider Native. Generated application code depends
only on Core Runtime and the native C++/OS runtime. The compiler is a normal
native build tool, not an application interpreter.

## CLI

~~~sh
iisdk-builder resources --manifest resources.json --platform macos \
  --cpp build/resources/app_assets.cpp --header build/resources/app_assets.h \
  --pack build/resources/app_assets.iirp --depfile build/resources/app_assets.d
~~~

For a large external pack, omit cpp/header:

~~~sh
iisdk-builder resources --manifest resources.json --platform android \
  --pack build/resources/app_assets.iirp
~~~

Supported selectors are windows, macos, linux, android, ios and wasm; host
selects the current desktop host. Outputs must stay under a build/ directory.
Generated cpp/header files share a directory. Inputs and output destinations
cannot alias, including resolved symlinks and native file-identity aliases on case-insensitive filesystems. Existing outputs are preserved on
validation/encoding failure; unchanged bytes preserve timestamps. Publication
of each output is atomic through FileProvider, but the group is not a single
transaction. Failure is an error; no partial compile is marked successful.

~~~json
{
  "schema": 1,
  "name": "app_assets",
  "maximumBytes": 536870912,
  "resources": [
    {"name": "icons/main", "source": "assets/common.svg"},
    {"name": "icons/main", "source": "assets/macos.svg", "platform": "macos"},
    {"name": "help/en.txt", "source": "assets/help.txt"}
  ]
}
~~~

Paths resolve relative to the manifest. The exact platform variant overrides
common data; unrelated platform entries are not opened or packed. Duplicate
name/platform pairs, unknown fields/platforms, noncanonical names and budgets
fail. The pack name is an ASCII identifier of at most 64 characters, with no
reserved double underscore. Logical names use strict case-sensitive UTF-8
paths; Core's complete name/format/lifetime contract is in
[resources.md](../../iiSDKCore/docs/resources.md).

The maximumBytes limit bounds the sum of selected input bytes and encoded bytes
separately, default 512 MiB each. C++ embedding additionally caps the encoded pack
at 32 MiB. Use binary packs/native mapping above that limit; no implicit output
format switch or compression occurs.

The successful command prints RESOURCE_BUILD_OK with target, entries,
unique_payloads, logical_bytes, stored_bytes and changed. Content deduplication
shares identical payloads; padding/index metadata can increase small packs.
These values measure package bytes, not process RSS or GPU storage.

## CMake

Include the installed module without finding/linking the Builder library:

~~~cmake
find_package(iiSDKCore 0.17 CONFIG REQUIRED COMPONENTS Runtime)
include("/host/Builder/lib/cmake/iiSDKBuilder/iiSDKBuilderResources.cmake")
add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE iiSDKCore::Runtime)
iisdkbuilder_add_resources(TARGET MyApp MANIFEST resources.json
    COMPILER "/host/Builder/bin/iisdk-builder")
~~~

An explicit COMPILER overrides IISDKBUILDER_RESOURCE_COMPILER. Native source/
installed Builder targets can supply the tool, then PATH is searched. Cross
builds require an explicit build-host executable; the helper never invokes a
target binary or an emulator. The CMake target system selects the platform,
including Android/iOS/WASM. Builder's application cache can pass the compiler
and module paths as ordinary cache entries.

The helper emits cpp/header/iirp/depfile under the application's binary tree,
adds the C++ source/include path and requires C++23. CMake reconfigures for
manifest changes. The compiler's depfile records selected source files with
Make-style escaping, including spaces, non-ASCII text, # and $. Asset changes
regenerate data; unchanged inputs do not invoke the compiler again. The actual
incremental Ninja path is covered by resources_integration.py.

~~~cpp
#include "app_assets.h"
auto pack=ii::resource::embedded::load_app_assets();
if (pack) {
    auto icon=pack->find("icons/main");
    if (icon && icon->verify()) {
        auto owned_view=*icon;
        // Keep owned_view alive while consuming owned_view.bytes().
    }
}
~~~

Generated arrays are aligned const data compiled by the target toolchain.
Windows/macOS/Linux/Android/iOS resources therefore live in the native machine
image; WASM resources live in the compiled WebAssembly module. Native OS images place these arrays in read-only sections. WASM uses the same
const API contract, but linear memory does not provide OS read-only page protection. Core does not unpack all payloads or execute
instructions from a resource archive. For mapped packs, the application obtains
a Channel from its storage provider and uses Core MappedRegion/Pack::open.

## Boundaries and verification

Native codec, texture, GPU shader, audio or model conversion belongs to its
owning SDK. Feed its prepared target bytes to this compiler. No lossy operation,
platform permissions, GPU compatibility or pixel quality is inferred from a
successful byte-pack build. The same logical resource may point to different
prepared native files per platform.

tests/resources.cpp covers target overrides/fallbacks, omitted missing files,
content reuse, malformed manifests, output/source preservation and deterministic
timestamps. resources_integration.py builds Core and a real HelloResources app
through Builder, runs it without source assets or helper runtimes, and verifies
changed/unchanged assets, manifest reconfiguration and escaped dependencies.
The independent relocated-package check uses copied application source and
installed Core/tool/module paths with upper SDK discovery disabled for the app.
Recorded results and platform execution boundaries are in Verification.md.

The Windows host CLI receives UTF-16 arguments through wmain and converts them to UTF-8 before common parsing. Generated resource identifiers remain ASCII, while manifest/input/output paths and logical resource names support UTF-8. Cross compilation alone does not establish Windows CLI execution.
