"""The Python client controls the native Builder process, not the target app."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys

tool, source_arg, core_arg, work_arg, cmake = sys.argv[1:]
source, core, work = map(Path, (source_arg, core_arg, work_arg))
spec=importlib.util.spec_from_file_location("iisdkbuilder", source/"src/Python/iisdkbuilder.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
app=work/"application"
shutil.copytree(source/"tests/fixtures/static-python-app",app)
host={"darwin":"macos","win32":"windows","linux":"linux"}[sys.platform]
manifest=work/"manifest.json"
manifest_data={"schema":1,"app":{"name":"PythonApiTest","source":str(app)},
    "sdkRoot":str(core.parent),"buildRoot":str(work/"build"),"cmake":cmake,
    "kits":[{"id":"host","platform":host}]}
manifest.write_text(json.dumps(manifest_data))
builder=module.Builder(tool)
plan=builder.plan(manifest)
assert plan["kits"][0]["projects"][0]["name"]=="iiSDKCore"
default_args=plan["kits"][0]["projects"][0]["commands"][0]["arguments"]
assert "-DBUILD_SHARED_LIBS=ON" in default_args
assert "-DIISDKCORE_STATIC_PYTHON_SOURCES=" in default_args
build_tool=app/"App/prepare.py"
build_tool.write_text("import os\nprint(os.getcwd())\n")
manifest_data["app"]["cache"]={"IISDKBUILDER_STATIC_NATIVE_AOT":"ON",
                               "IISDKBUILDER_BUILD_TIME_PYTHON":"App/prepare.py"}
manifest.write_text(json.dumps(manifest_data))
plan=builder.plan(manifest)
args=plan["kits"][0]["projects"][0]["commands"][0]["arguments"]
sources=next(arg for arg in args if arg.startswith("-DIISDKCORE_STATIC_PYTHON_SOURCES="))
assert "transform.py" in sources and "prepare.py" not in sources
app_args=plan["kits"][0]["projects"][-1]["commands"][0]["arguments"]
assert not any(arg.startswith("-DIISDKBUILDER_BUILD_TIME_PYTHON=") for arg in app_args)
manifest_data["app"]["cache"]["IISDKBUILDER_BUILD_TIME_PYTHON"]="App/missing.py"
manifest.write_text(json.dumps(manifest_data))
try:
    builder.plan(manifest)
    raise AssertionError("unknown build-time Python source accepted")
except module.BuilderError:
    pass
manifest_data["app"]["cache"]["IISDKBUILDER_BUILD_TIME_PYTHON"]="App/prepare.py;App/prepare.py"
manifest.write_text(json.dumps(manifest_data))
try:
    builder.plan(manifest)
    raise AssertionError("duplicate build-time Python source accepted")
except module.BuilderError:
    pass
manifest_data["app"]["cache"]["IISDKBUILDER_BUILD_TIME_PYTHON"]="App/prepare.py"
manifest.write_text(json.dumps(manifest_data))
unsupported=app/"App/legacy.lua"
unsupported.write_text("return 1\n")
try:
    builder.plan(manifest)
    raise AssertionError("unsupported dynamic source accepted")
except module.BuilderError:
    pass
try:
    builder.plan(work/"missing.json")
    raise AssertionError("missing manifest accepted")
except module.BuilderError:
    pass
print("Builder Python client passed")
