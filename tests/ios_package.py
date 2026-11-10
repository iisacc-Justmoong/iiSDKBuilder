"""Build iOS-simulator Mach-O frameworks and inspect an unsigned app bundle."""
from pathlib import Path
import plistlib
import subprocess
import sys

tool, work, script = sys.argv[1:]
work=Path(work);work.mkdir(parents=True)
def run(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=90)
    assert result.returncode==0, f'{args}\n{result.stdout}\n{result.stderr}'
    return result.stdout
sdk=run(['xcrun','--sdk','iphonesimulator','--show-sdk-path']).strip()
clang=run(['xcrun','--sdk','iphonesimulator','--find','clang']).strip()
prefix=work/'sdk frameworks';prefix.mkdir()
def framework(name,code,dependencies=()):
    bundle=prefix/(name+'.framework');bundle.mkdir()
    src=work/(name+'.c');src.write_text(code)
    (bundle/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'com.example.'+name,
        'CFBundleExecutable':name,'CFBundleName':name,'CFBundlePackageType':'FMWK','CFBundleVersion':'1'}))
    args=[clang,'-dynamiclib','-arch','arm64','-isysroot',sdk,'-mios-simulator-version-min=15.0',
          '-Wl,-install_name,@rpath/'+name+'.framework/'+name,'-F'+str(prefix),src]
    for dep in dependencies: args += ['-framework',dep]
    run(args+['-o',bundle/name]);return bundle
framework('Base','int base(void) { return 7; }\n')
framework('Used','extern int base(void); int used(void) { return base(); }\n',['Base'])
framework('Unused','int unused(void) { return 0; }\n')
app=work/'Fixture.app';app.mkdir()
(app/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier':'com.example.fixture',
    'CFBundleExecutable':'Fixture','CFBundleName':'Fixture','CFBundlePackageType':'APPL','CFBundleVersion':'1'}))
main=work/'main.c';main.write_text('extern int used(void); int main(void) { return used(); }\n')
run([clang,'-arch','arm64','-isysroot',sdk,'-mios-simulator-version-min=15.0','-F'+str(prefix),main,
     '-framework','Used','-Wl,-rpath,'+str(prefix),'-o',app/'Fixture'])
dest=work/'package'
args=[tool,'-DINPUT='+str(app/'Fixture'),'-DDESTINATION='+str(dest),'-DSEARCH_DIRS='+str(prefix),'-P',script]
run(args)
output=dest/'Fixture.app';frameworks=output/'Frameworks'
assert (output/'Fixture').is_file() and (frameworks/'Used.framework/Used').is_file()
assert (frameworks/'Base.framework/Base').is_file() and not (frameworks/'Unused.framework').exists()
for binary in (output/'Fixture',frameworks/'Used.framework/Used',frameworks/'Base.framework/Base'):
    deps='\n'.join(run(['otool','-L',binary]).splitlines()[1:])
    assert str(prefix) not in deps, deps
assert '@executable_path/Frameworks/Used.framework/Used' in run(['otool','-L',output/'Fixture'])
# A macOS framework with the same name and architecture is not an iOS framework.
base=prefix/'Base.framework/Base'
ios_base=base.read_bytes()
run(['clang','-dynamiclib','-arch','arm64',work/'Base.c',
     '-Wl,-install_name,@rpath/Base.framework/Base','-o',base])
failed=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=90)
assert failed.returncode!=0 and (output/'Fixture').is_file(), 'Accepted a macOS framework into an iOS app'
base.write_bytes(ios_base)
# A missing dependency must not replace an existing successful package.
(prefix/'Base.framework/Base').unlink()
failed=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=90)
assert failed.returncode!=0 and (output/'Fixture').is_file()
print('iOS simulator framework dependency closure and unsigned bundle inspected')
