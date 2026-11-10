"""Cross-compile and inspect load-time Wasm side-module packaging."""
import json
from pathlib import Path
import subprocess
import sys
from builder_subprocess import run_builder

tool, work, cmake, toolchain = sys.argv[1:]
work=Path(work);core=work/'sdk/iiSDKCore';addon=work/'sdk/Addon';app=work/'app'
for directory in (core,addon,app): directory.mkdir(parents=True)
(core/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(CoreFixture LANGUAGES CXX)
add_library(Core INTERFACE)
set_target_properties(Core PROPERTIES EXPORT_NAME iiSDKCore)
install(TARGETS Core EXPORT CoreTargets)
install(EXPORT CoreTargets NAMESPACE iiSDKCore:: DESTINATION lib/cmake/iiSDKCore)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake" "include(\\"\\${CMAKE_CURRENT_LIST_DIR}/CoreTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake" DESTINATION lib/cmake/iiSDKCore)
''')
(addon/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(Addon LANGUAGES CXX)
add_library(Used used.cpp)
add_library(Unused unused.cpp)
install(TARGETS Used Unused EXPORT AddonTargets LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(EXPORT AddonTargets NAMESPACE Addon:: DESTINATION lib/cmake/Addon)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" "include(\\"\\${CMAKE_CURRENT_LIST_DIR}/AddonTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" DESTINATION lib/cmake/Addon)
''')
(addon/'used.cpp').write_text('extern "C" int used() { return 7; }\n')
(addon/'unused.cpp').write_text('extern "C" int unused() { return 0; }\n')
(app/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(WasmFixture LANGUAGES CXX)
find_package(Addon CONFIG REQUIRED)
add_executable(WasmFixture main.cpp)
target_link_libraries(WasmFixture PRIVATE Addon::Used)
''')
(app/'main.cpp').write_text('extern "C" int used(); int main() { return used(); }\n')
manifest=work/'request.json'
manifest.write_text(json.dumps({'schema':1,'app':{'name':'WasmFixture','source':str(app),'targets':['WasmFixture']},
    'sdkRoot':str(work/'sdk'),'sdks':[{'name':'Addon','source':str(addon)}],
    'buildRoot':str(work/'build'),'cmake':cmake,'jobs':4,'parallelKits':1,
    'kits':[{'id':'wasm','platform':'wasm','generator':'Ninja','toolchain':toolchain}]}))
result=run_builder([tool,'build','--manifest',str(manifest)],timeout=180)
if result.returncode:
    report=json.loads(result.stdout)
    logs=[Path(step['log']).read_text(errors='replace') for kit in report['kits'] for step in kit['steps']
          if step['status']=='failed' and Path(step['log']).is_file()]
    raise AssertionError(f'{result.stdout}\n{result.stderr}\n{logs}')
report=json.loads(result.stdout);kit=report['kits'][0]
assert report['succeeded'] and kit['packaging']=='succeeded' and kit['execution']=='not_requested',kit
package=Path(kit['packages'][0]);files={path.name for path in package.rglob('*') if path.is_file()}
assert 'WasmFixture.js' in files and 'WasmFixture.wasm' in files and 'libUsed.so' in files,files
assert 'libUnused.so' not in files,files
print('Builder WebAssembly main/side modules and loader packaged without execution')
