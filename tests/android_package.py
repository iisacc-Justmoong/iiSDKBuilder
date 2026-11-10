"""Compile Android ELF fixtures and inspect an unsigned APK without launching it."""
import os
from pathlib import Path
import subprocess
import sys
import zipfile

tool, work, sdk, ndk, script = map(Path, sys.argv[1:])
work.mkdir(parents=True, exist_ok=True)
prefix = work / 'sdk prefix' / 'lib'
prefix.mkdir(parents=True)
manifest = work / 'AndroidManifest.xml'
manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.example.nativefixture">
<uses-sdk android:minSdkVersion="29" android:targetSdkVersion="35" />
<application android:hasCode="false" android:extractNativeLibs="true" android:label="Fixture">
<activity android:name="android.app.NativeActivity" android:exported="true">
<meta-data android:name="android.app.lib_name" android:value="nativefixture" />
<intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
</activity></application></manifest>''')
host = {'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64', 'win32': 'windows-x86_64'}[sys.platform]
clang = ndk / 'toolchains/llvm/prebuilt' / host / 'bin/clang'
if sys.platform == 'win32': clang = clang.with_suffix('.exe')
def run(args):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, f'{args}\n{result.stdout}\n{result.stderr}'
    return result.stdout

def library(name, source, extra=()):
    src = work / (name + '.c'); src.write_text(source)
    output = prefix / ('lib' + name + '.so')
    run([clang, '--target=aarch64-linux-android29', '-fPIC', '-shared', '-nostdlib',
         '-Wl,-soname,' + output.name, src, '-L' + str(prefix), *extra, '-o', output])
    return output
base = library('base', 'int base(void) { return 2; }\n')
used = library('used', 'extern int base(void); int used(void) { return base(); }\n', ['-lbase'])
unused = library('unused', 'int unused(void) { return 0; }\n')
app = library('nativefixture', 'extern int used(void); int entry(void) { return used(); }\n', ['-lused'])
dest = work / 'package'
args = [tool, '-DINPUT=' + str(app), '-DDESTINATION=' + str(dest), '-DANDROID_ABI=arm64-v8a',
        '-DANDROID_API=29', '-DANDROID_NDK=' + str(ndk), '-DANDROID_SDK=' + str(sdk),
        '-DMANIFEST=' + str(manifest), '-DSEARCH_DIRS=' + str(prefix), '-P', script]
run(args)
apk = dest / 'app-unsigned.apk'
assert apk.is_file(), 'unsigned APK missing'
with zipfile.ZipFile(apk) as archive:
    names = set(archive.namelist())
    for required in ('AndroidManifest.xml','lib/arm64-v8a/libnativefixture.so',
                     'lib/arm64-v8a/libused.so','lib/arm64-v8a/libbase.so'):
        assert required in names, names
    assert 'lib/arm64-v8a/libunused.so' not in names, names
    assert archive.getinfo('lib/arm64-v8a/libnativefixture.so').compress_type == zipfile.ZIP_STORED
align=sorted((sdk/'build-tools').glob('*/zipalign'))[-1]
run([align,'-c','-P','16','4',apk])
# A missing dependency must not replace the preceding successful APK.
base.unlink()
failed = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=90)
assert failed.returncode != 0, 'missing ELF dependency was accepted'
assert apk.is_file(), 'previous package was not preserved'
# An XML comment must not disguise a manifest that actually requires DEX.
base = library('base', 'int base(void) { return 2; }\n')
manifest.write_text(manifest.read_text().replace('android:hasCode="false"',
    'android:hasCode="true"').replace('<application', '<!-- android:hasCode="false" --><application'))
malicious = [str(arg).replace('-DDESTINATION='+str(dest), '-DDESTINATION='+str(work/'misleading')) for arg in args]
failed = subprocess.run(list(map(str, malicious)), capture_output=True, text=True, timeout=90)
assert failed.returncode != 0, 'manifest requiring DEX was accepted based on a comment'
print('Android ABI closure and unsigned APK structure passed')
