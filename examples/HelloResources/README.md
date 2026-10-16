# HelloResources

This C++23 app reads embedded greeting/icon/platform resources through Core,
checks fingerprints and verifies two logical names share one payload address.
The same main.cpp is used for all target platforms. The SVG is opaque resource
data here; this example does not render it or claim a graphics backend.

Build with installed Core 0.17 and a build-host Builder 0.2:

~~~sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Core \
  -DIISDKBUILDER_RESOURCE_COMPILER=/host/Builder/bin/iisdk-builder
cmake --build build
ctest --test-dir build --output-on-failure
~~~

The source checkout locates Builder's CMake module relative to this example.
For a copied/standalone application, pass
IISDKBUILDER_RESOURCES_MODULE=/host/Builder/lib/cmake/iiSDKBuilder/iiSDKBuilderResources.cmake.
Cross builds use the target Core package/toolchain while the compiler/module
come from the build host. No Builder/FileProvider library is linked into the app.

To build through Builder's matrix, select this directory as app.source, target
HelloResources, and pass both helper paths through app.cache. Core is configured,
built and installed first; then the application is configured and built.
resources_integration.py reproduces this path with five successful steps on the
native host. Tests do not run foreign target executables automatically.

On Android the example produces a native shared library; it is not a Java or
Android UI host. iOS produces a bundle for compilation; signed device execution
requires a real application host. WASM emits a compiled module with normal
Emscripten glue. See the module's verification record for actual execution.
