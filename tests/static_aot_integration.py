"""Compile two formerly dynamic plugin sources into the app-specific Core."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, source_arg, core_arg, work_arg, cmake = sys.argv[1:]
source, core, work = map(Path, (source_arg, core_arg, work_arg))
app = work / "application with spaces"
shutil.copytree(source / "tests/fixtures/static-app", app)
host = {"darwin": "macos", "win32": "windows", "linux": "linux"}[sys.platform]
manifest = work / "static-manifest.json"
manifest.write_text(json.dumps({"schema": 1,
    "app": {"name": "StaticPluginApp", "source": str(app), "targets": ["static_app"], "cache": {"IISDKBUILDER_STATIC_NATIVE_AOT": "ON"}},
    "sdkRoot": str(core.parent), "buildRoot": str(work / "build"), "cmake": cmake,
    "jobs": 4, "parallelKits": 1, "kits": [{"id": "host", "platform": host}]}))
def invoke(action):
    result=subprocess.run([tool,action,"--manifest",str(manifest)],capture_output=True,text=True,timeout=300)
    if result.returncode:
        raise AssertionError(f"{action}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)
plan=invoke("plan")
core_project=plan["kits"][0]["projects"][0]
args=core_project["commands"][0]["arguments"]
assert "-DIISDKCORE_STATIC_PLUGINS=ON" in args, "Core plugin loading was not disabled"
sources=next((arg for arg in args if arg.startswith("-DIISDKCORE_STATIC_PLUGIN_SOURCES=")),"")
assert all(str(app/"App/Plugins"/name) in sources for name in ("alpha.cpp","beta.cpp","library.cpp")), "plugin sources were not discovered"
report=invoke("build")
assert report["succeeded"], report
exe=Path(report["kits"][0]["artifacts"][0])
archive_dir=work/"build/kits/host/Release/prefix/iiSDKCore/lib"
archives=[p for p in (archive_dir/"libiiSDKCore.a",archive_dir/"iiSDKCore.lib") if p.is_file()]
assert len(archives)==1, "app-specific Core static library missing"
if sys.platform!="win32":
    members=subprocess.run(["ar","-t",str(archives[0])],capture_output=True,text=True,check=True).stdout
    assert all(name in members for name in ("alpha.cpp","beta.cpp","library.cpp")), "plugin objects were not merged into Core"
native_env=os.environ.copy()
for name in ("PYTHONHOME","PYTHONPATH","LD_LIBRARY_PATH","DYLD_LIBRARY_PATH","DYLD_FALLBACK_LIBRARY_PATH"):
    native_env.pop(name,None)
native_env["PATH"]=""
if os.environ.get("IISDKBUILDER_TEST_BUILD_ONLY") != "1":
    completed=subprocess.run([str(exe)],cwd=work,env=native_env,capture_output=True,text=True,timeout=20)
    assert completed.returncode==0 and "STATIC_PLUGIN_APP_OK" in completed.stdout, completed.stderr
for pattern in ("*alpha*.dylib","*beta*.dylib","*alpha*.so","*beta*.so","*alpha*.dll","*beta*.dll"):
    assert not list(work.rglob(pattern)), "a plugin shared library was emitted"
if sys.platform=="darwin":
    symbols=subprocess.run(["nm","-u",str(exe)],capture_output=True,text=True,check=True).stdout
    assert not any(line.strip() in {"_dlopen","_dlsym"} for line in symbols.splitlines()), "dynamic plugin loader remained in app"
    dependencies=subprocess.run(["otool","-L",str(exe)],capture_output=True,text=True,check=True).stdout
    assert "libiiSDKCore" not in dependencies, "the app-specific Core still requires a shared Core component"
print("static plugin discovery and Core archive checks passed")
