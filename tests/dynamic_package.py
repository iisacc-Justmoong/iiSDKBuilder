"""Build a linked/transitive/unused fixture; inspect packaging without running it."""
import json
import shutil
from pathlib import Path
import subprocess
import sys

tool, root_arg, cmake = sys.argv[1:]
root = Path(root_arg)
core = root / 'sdk' / 'iiSDKCore'
addon = root / 'sdk' / 'Addon'
app = root / 'app'
for directory in (core, addon, app):
    directory.mkdir(parents=True)
(core / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(CoreFixture LANGUAGES CXX)
add_library(Core INTERFACE)
set_target_properties(Core PROPERTIES EXPORT_NAME iiSDKCore)
install(TARGETS Core EXPORT CoreTargets)
install(EXPORT CoreTargets NAMESPACE iiSDKCore:: DESTINATION lib/cmake/iiSDKCore)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake" "include(\\"${CMAKE_CURRENT_LIST_DIR}/CoreTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake" DESTINATION lib/cmake/iiSDKCore)
'''.replace('${CMAKE_CURRENT_LIST_DIR}/CoreTargets', '\\${CMAKE_CURRENT_LIST_DIR}/CoreTargets'))
(addon / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(Addon LANGUAGES CXX)
add_library(Transitive transitive.cpp)
add_library(Used used.cpp)
add_library(Unused unused.cpp)
add_library(OptionalPlugin plugin.cpp)
add_library(StaticSdk STATIC static.cpp)
target_link_libraries(Used PRIVATE Transitive)
target_link_libraries(OptionalPlugin PRIVATE Transitive)
set_target_properties(Transitive Used Unused OptionalPlugin PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
install(TARGETS Used Transitive Unused OptionalPlugin StaticSdk EXPORT AddonTargets RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(EXPORT AddonTargets NAMESPACE Addon:: DESTINATION lib/cmake/Addon)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" "include(\\"\\${CMAKE_CURRENT_LIST_DIR}/AddonTargets.cmake\\")")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" DESTINATION lib/cmake/Addon)
''')
(addon / 'transitive.cpp').write_text('extern "C" int transitive() { return 2; }\n')
(addon / 'used.cpp').write_text('extern "C" int transitive(); extern "C" int used() { return transitive(); }\n')
(addon / 'unused.cpp').write_text('extern "C" int unused() { return 0; }\n')
(addon / 'plugin.cpp').write_text('extern "C" int transitive(); extern "C" int plugin() { return transitive(); }\n')
(addon / 'static.cpp').write_text('extern "C" int static_sdk() { return 0; }\n')
(app / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.31)
project(PackageFixture LANGUAGES CXX)
find_package(Addon CONFIG REQUIRED)
add_executable(PackageFixture main.cpp)
target_link_libraries(PackageFixture PRIVATE Addon::Used Addon::Unused)
option(LINK_STATIC_SDK "Exercise static linkage reporting" OFF)
if(LINK_STATIC_SDK)
    target_link_libraries(PackageFixture PRIVATE Addon::StaticSdk)
endif()
if(APPLE)
    add_executable(BundleFixture MACOSX_BUNDLE main.cpp)
    target_link_libraries(BundleFixture PRIVATE Addon::Used Addon::Unused)
    add_custom_command(TARGET BundleFixture POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:BundleFixture>/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:Addon::Unused>" "$<TARGET_BUNDLE_CONTENT_DIR:BundleFixture>/Frameworks/$<TARGET_FILE_NAME:Addon::Unused>")
endif()
''')
(app / 'main.cpp').write_text('extern "C" int used(); int main() { return used() == 2 ? 0 : 1; }\n')
host = {'darwin': 'macos', 'win32': 'windows', 'linux': 'linux'}[sys.platform]
manifest = root / 'manifest.json'
data = {'schema': 1, 'app': {'name': 'PackageFixture', 'source': str(app), 'depends': ['Addon'], 'targets': ['PackageFixture']},
    'sdkRoot': str(root / 'sdk'), 'sdks': [{'name': 'Addon', 'source': str(addon)}],
    'buildRoot': str(root / 'build'), 'cmake': cmake, 'jobs': 4, 'parallelKits': 1,
    'kits': [{'id': 'host', 'platform': host}]}
manifest.write_text(json.dumps(data))
def build():
    result = subprocess.run([tool, 'build', '--manifest', str(manifest)], capture_output=True, text=True, timeout=180)
    if result.returncode != 0:
        try:
            failed = json.loads(result.stdout)
            logs = [Path(step['log']).read_text(errors='replace') for kit in failed.get('kits', [])
                    for step in kit.get('steps', []) if step['status'] == 'failed' and Path(step['log']).is_file()]
        except (ValueError, KeyError):
            logs = []
        raise AssertionError(f'{result.stdout}\n{result.stderr}\n{logs}')
    report = json.loads(result.stdout)
    assert report['succeeded'], report
    kit = report['kits'][0]
    assert kit['packaging'] == 'succeeded', kit
    assert kit['execution'] == 'not_requested', kit
    assert len(kit['packages']) == 1, kit
    return Path(kit['packages'][0]), kit
package, kit = build()
assert package.is_dir(), package
files = [p for p in package.rglob('*') if p.is_file()]
names = [p.name.lower() for p in files]
assert any('used' in n and 'unused' not in n for n in names), names
assert any('transitive' in n for n in names), names
assert not any('unused' in n or 'optionalplugin' in n for n in names), names
assert kit['linkage'] == 'shared_requested' and not kit['staticSdkLinks'], kit
# A previous interrupted transaction may contain data; never remove it implicitly.
interrupted = Path(str(package) + '.staging')
interrupted.mkdir()
(interrupted / '.iisdkbuilder-package').write_text('previous transaction')
(interrupted / 'keep').write_text('preserve')
blocked = subprocess.run([tool, 'build', '--manifest', str(manifest)], capture_output=True, text=True, timeout=180)
assert blocked.returncode != 0 and (interrupted / 'keep').read_text() == 'preserve', blocked.stdout
shutil.rmtree(interrupted)
# An app may explicitly retain link-only libraries whose initializer is its API.
data['app']['cache'] = {'IISDKBUILDER_PRUNE_UNUSED_SHARED': 'OFF'}
manifest.write_text(json.dumps(data))
_, retained_kit = build()
assert any('unused' in p.name.lower() for p in package.rglob('*')), 'explicit retention was ignored'
data['app']['cache'] = {'IISDKBUILDER_PRUNE_UNUSED_SHARED': 'ON'}
manifest.write_text(json.dumps(data))
assert build()[0] == package
assert not any('unused' in p.name.lower() for p in package.rglob('*')), 'unused dependency was not pruned'
if sys.platform == 'darwin':
    for image in [p for p in files if p.name == 'PackageFixture' or p.suffix == '.dylib']:
        listing = subprocess.run(['otool', '-L', str(image)], check=True, capture_output=True, text=True).stdout
        deps = '\n'.join(listing.splitlines()[1:])
        assert str(root / 'build') not in deps, deps
        assert str(root / 'sdk') not in deps, deps
        assert '/Users/' not in deps, deps
    assert any('@loader_path/../lib/' in subprocess.run(['otool', '-L', str(p)], check=True, capture_output=True, text=True).stdout for p in files if p.name == 'PackageFixture')
(package / 'stale-from-previous-package').write_text('old')
assert build()[0] == package
assert not (package / 'stale-from-previous-package').exists(), 'stale package content was retained'
plugin_name = {'darwin': 'libOptionalPlugin.dylib', 'linux': 'libOptionalPlugin.so', 'win32': 'OptionalPlugin.dll'}[sys.platform]
data['app']['cache'] = {'IISDKBUILDER_RUNTIME_MODULES': plugin_name}
manifest.write_text(json.dumps(data))
plugin_package, kit = build()
assert plugin_package == package and any('optionalplugin' in p.name.lower() for p in package.rglob('*'))
if sys.platform != 'win32':
    data['app']['cache']['LINK_STATIC_SDK'] = 'ON'
    manifest.write_text(json.dumps(data))
    failed_static = subprocess.run([tool, 'build', '--manifest', str(manifest)], capture_output=True, text=True, timeout=180)
    assert failed_static.returncode != 0, failed_static.stdout
    failed_kit = json.loads(failed_static.stdout)['kits'][0]
    assert failed_kit['linkage'] == 'contains_static_sdk' and any('StaticSdk' in link for link in failed_kit['staticSdkLinks']), failed_kit
    data['app']['cache']['IISDKBUILDER_ALLOW_STATIC_MODULES'] = 'ON'
    manifest.write_text(json.dumps(data))
    _, kit = build()
    assert kit['linkage'] == 'contains_static_sdk', kit
data['app']['cache'] = {'LINK_STATIC_SDK': 'OFF'}
manifest.write_text(json.dumps(data))
assert build()[0] == package
assert not any('optionalplugin' in p.name.lower() for p in package.rglob('*')), 'undeclared plugin was retained'
# A missing declared plugin must fail without replacing the prior successful package.
data['app']['cache'] = {'IISDKBUILDER_RUNTIME_MODULES': 'missing-sdk-plugin.dylib'}
manifest.write_text(json.dumps(data))
failed = subprocess.run([tool, 'build', '--manifest', str(manifest)], capture_output=True, text=True, timeout=180)
assert failed.returncode != 0 and not json.loads(failed.stdout)['succeeded'], failed.stdout
assert (package / '.iisdkbuilder-package').is_file()
data['app']['cache'] = {'IISDKBUILDER_PACKAGE': 'OFF'}
manifest.write_text(json.dumps(data))
compile_only = subprocess.run([tool, 'build', '--manifest', str(manifest)], capture_output=True, text=True, timeout=180)
assert compile_only.returncode == 0, compile_only.stdout + compile_only.stderr
compile_kit = json.loads(compile_only.stdout)['kits'][0]
assert compile_kit['packaging'] == 'not_requested' and compile_kit['packages'] == []
data['app']['cache'] = {}
manifest.write_text(json.dumps(data))
if sys.platform == 'darwin':
    data['app']['targets'] = ['BundleFixture']
    manifest.write_text(json.dumps(data))
    bundle_package, kit = build()
    bundle = bundle_package / 'BundleFixture.app'
    assert (bundle / 'Contents' / 'MacOS' / 'BundleFixture').is_file()
    frameworks = bundle / 'Contents' / 'Frameworks'
    assert any('used' in p.name.lower() and 'unused' not in p.name.lower() for p in frameworks.iterdir())
    assert any('transitive' in p.name.lower() for p in frameworks.iterdir())
    assert not any('unused' in p.name.lower() for p in frameworks.iterdir())
    subprocess.run(['codesign', '--verify', '--strict', str(bundle)], check=True, capture_output=True)
print('used and transitive libraries bundled; unused excluded; package linkage inspected')
