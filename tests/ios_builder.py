"""Cross-build an iOS-simulator app and inspect its private frameworks."""
import json
from pathlib import Path
import subprocess
import sys
from builder_subprocess import run_builder

tool, work, cmake = sys.argv[1:]
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
set_target_properties(Used Unused PROPERTIES FRAMEWORK TRUE FRAMEWORK_VERSION A)
install(TARGETS Used Unused EXPORT AddonTargets FRAMEWORK DESTINATION lib LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(EXPORT AddonTargets NAMESPACE Addon:: DESTINATION lib/cmake/Addon)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" "include(\\"\\${CMAKE_CURRENT_LIST_DIR}/AddonTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" DESTINATION lib/cmake/Addon)
''')
(addon/'used.cpp').write_text('extern "C" int used() { return 7; }\n')
(addon/'unused.cpp').write_text('extern "C" int unused() { return 0; }\n')
(app/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(IOSFixture LANGUAGES CXX)
find_package(Addon CONFIG REQUIRED)
add_executable(IOSFixture MACOSX_BUNDLE main.cpp)
target_link_libraries(IOSFixture PRIVATE Addon::Used Addon::Unused)
set_target_properties(IOSFixture PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.iosfixture")
''')
(app/'main.cpp').write_text('extern "C" int used(); int main() { return used(); }\n')
manifest=work/'request.json'
manifest.write_text(json.dumps({'schema':1,'app':{'name':'IOSFixture','source':str(app),'targets':['IOSFixture']},
    'sdkRoot':str(work/'sdk'),'sdks':[{'name':'Addon','source':str(addon)}],
    'buildRoot':str(work/'build'),'cmake':cmake,'jobs':4,'parallelKits':1,
    'kits':[{'id':'ios','platform':'ios','generator':'Ninja','architecture':'arm64','sysroot':'iphonesimulator'}]}))
result=run_builder([tool,'build','--manifest',str(manifest)],timeout=180)
if result.returncode:
    report=json.loads(result.stdout)
    logs=[Path(step['log']).read_text(errors='replace') for kit in report['kits'] for step in kit['steps']
          if step['status']=='failed' and Path(step['log']).is_file()]
    raise AssertionError(f'{result.stdout}\n{result.stderr}\n{logs}')
report=json.loads(result.stdout);kit=report['kits'][0]
assert report['succeeded'] and kit['packaging']=='succeeded' and kit['packageSigning']=='unsigned',kit
bundle=Path(kit['packages'][0])/'IOSFixture.app'
assert (bundle/'IOSFixture').is_file() and (bundle/'Frameworks/Used.framework/Used').is_file()
assert not (bundle/'Frameworks/Unused.framework').exists()
assert kit['execution']=='not_requested'
print('Builder iOS simulator cross-build and unsigned framework bundle inspected')
