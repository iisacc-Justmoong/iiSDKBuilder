"""Build and execute an app whose Python subset becomes a native Core object."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

tool, source_arg, core_arg, work_arg, cmake = sys.argv[1:]
source, core, work = map(Path, (source_arg, core_arg, work_arg))
app = work / "python application"
shutil.copytree(source / "tests/fixtures/static-python-app", app)
(app/"App/prepare.py").write_text("import os\nprint(os.getcwd())\n",encoding="utf-8")
host = {"darwin": "macos", "win32": "windows", "linux": "linux"}[sys.platform]
manifest = work / "static-python.json"
manifest.write_text(json.dumps({"schema":1,"app":{"name":"StaticPythonApp","source":str(app),"targets":["static_python_app"],
    "cache":{"IISDKBUILDER_BUILD_TIME_PYTHON":"App/prepare.py", "IISDKBUILDER_STATIC_NATIVE_AOT":"ON"}},
    "sdkRoot":str(core.parent),"buildRoot":str(work/"build"),"cmake":cmake,"jobs":4,"parallelKits":1,
    "kits":[{"id":"host","platform":host}]}))
def invoke(action):
    result=subprocess.run([tool,action,"--manifest",str(manifest)],capture_output=True,text=True,timeout=300)
    if result.returncode:
        raise AssertionError(f"{action}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)
plan=invoke("plan")
args=plan["kits"][0]["projects"][0]["commands"][0]["arguments"]
sources=next((arg for arg in args if arg.startswith("-DIISDKCORE_STATIC_PYTHON_SOURCES=")),"")
assert all(str(app/"App"/name) in sources for name in ("transform.py","modulo.py")), "Python sources not discovered for native compilation"
assert "prepare.py" not in sources, "host-only generator was treated as runtime Python"
report=invoke("build")
assert report["succeeded"], report
exe=Path(report["kits"][0]["artifacts"][0])
env=os.environ.copy()
for name in ("PYTHONHOME","PYTHONPATH","LD_LIBRARY_PATH","DYLD_LIBRARY_PATH","DYLD_FALLBACK_LIBRARY_PATH"):
    env.pop(name,None)
env["PATH"]=""
if os.environ.get("IISDKBUILDER_TEST_BUILD_ONLY") != "1":
    result=subprocess.run([str(exe)],cwd=work,env=env,capture_output=True,text=True,timeout=20)
    assert result.returncode==0 and "STATIC_PYTHON_APP_OK" in result.stdout, result.stderr
if sys.platform=="darwin":
    libraries=subprocess.run(["otool","-L",str(exe)],capture_output=True,text=True,check=True).stdout.lower().splitlines()[1:]
    symbols=subprocess.run(["nm","-u",str(exe)],capture_output=True,text=True,check=True).stdout
    assert not any("libpython" in line or "/python.framework/" in line for line in libraries), libraries
    assert not any(line.strip().startswith("_Py_") or line.strip() == "_dlopen" for line in symbols.splitlines()), symbols
    assert not any("libiisdkcore" in line for line in libraries), "Core is not fully statically linked"
script=app/"App/transform.py"
script.write_text("import os\ndef transform(value: int) -> int:\n    return value\n",encoding="utf-8")
failed=subprocess.run([tool,"build","--manifest",str(manifest)],capture_output=True,text=True,timeout=300)
assert failed.returncode!=0, "unsupported Python fell back to an interpreter"
failure=json.loads(failed.stdout)
assert not failure["succeeded"]
logs=[Path(step["log"]) for step in failure["kits"][0]["steps"] if step["status"]=="failed"]
assert logs and any("native Python AOT" in log.read_text(encoding="utf-8") for log in logs)
print("Python subset compiled into Core and binary dependencies inspected")
