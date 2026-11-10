"""Cross-build an Android app and SDK and inspect the Builder-produced APK."""
import json
from pathlib import Path
import subprocess
import sys
import zipfile
from builder_subprocess import run_builder

tool, work, sdk, ndk, cmake = sys.argv[1:]
work, sdk, ndk = map(Path, (work, sdk, ndk))
core, addon, app = work/'sdk/iiSDKCore', work/'sdk/Addon', work/'app'
for directory in (core, addon, app): directory.mkdir(parents=True)
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
install(TARGETS Used Unused EXPORT AddonTargets LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
install(EXPORT AddonTargets NAMESPACE Addon:: DESTINATION lib/cmake/Addon)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" "include(\\"\\${CMAKE_CURRENT_LIST_DIR}/AddonTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" DESTINATION lib/cmake/Addon)
''')
(addon/'used.cpp').write_text('extern "C" int used() { return 7; }\n')
(addon/'unused.cpp').write_text('extern "C" int unused() { return 0; }\n')
(app/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(AndroidFixture LANGUAGES CXX)
find_package(Addon CONFIG REQUIRED)
add_library(nativefixture SHARED main.cpp)
target_link_libraries(nativefixture PRIVATE Addon::Used Addon::Unused)
''')
(app/'main.cpp').write_text('''#include <android/native_activity.h>
extern "C" int used();
extern "C" void ANativeActivity_onCreate(ANativeActivity*, void*, size_t) { (void)used(); }
''')
(app/'AndroidManifest.xml').write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.example.androidfixture">
<uses-sdk android:minSdkVersion="29" android:targetSdkVersion="35"/>
<application android:hasCode="false" android:extractNativeLibs="true" android:label="Fixture">
<activity android:name="android.app.NativeActivity" android:exported="true">
<meta-data android:name="android.app.lib_name" android:value="nativefixture"/>
<intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
</activity></application></manifest>''')
manifest=work/'request.json'
manifest.write_text(json.dumps({'schema':1,'app':{'name':'AndroidFixture','source':str(app),'targets':['nativefixture'],
    'cache':{'IISDKBUILDER_ANDROID_SDK':str(sdk)}},'sdkRoot':str(work/'sdk'),
    'sdks':[{'name':'Addon','source':str(addon)}],'buildRoot':str(work/'build'),'cmake':cmake,'jobs':4,'parallelKits':1,
    'kits':[{'id':'android','platform':'android','androidAbi':'arm64-v8a','androidApi':29,
             'toolchain':str(ndk/'build/cmake/android.toolchain.cmake')}]}))
result=run_builder([tool,'build','--manifest',str(manifest)],timeout=180)
if result.returncode:
    report=json.loads(result.stdout)
    logs=[Path(step['log']).read_text(errors='replace') for kit in report['kits'] for step in kit['steps']
          if step['status']=='failed' and Path(step['log']).is_file()]
    raise AssertionError(f'{result.stderr}\n{logs}')
report=json.loads(result.stdout);kit=report['kits'][0]
assert report['succeeded'] and kit['packaging']=='succeeded' and kit['packageSigning']=='unsigned', kit
apk=Path(kit['packages'][0])/'app-unsigned.apk'
with zipfile.ZipFile(apk) as archive:
    names=set(archive.namelist())
    assert 'lib/arm64-v8a/libnativefixture.so' in names and 'lib/arm64-v8a/libUsed.so' in names, names
    assert 'lib/arm64-v8a/libUnused.so' not in names, names
assert kit['execution']=='not_requested'
print('Builder Android cross-build and unsigned APK inspected')
