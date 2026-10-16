"""Run real CMake SDK -> addon -> application builds and a mismatched kit."""
import json
import os
import pathlib
import shutil
import subprocess
import sys

tool, source_arg, core_arg, output_arg, cmake = sys.argv[1:]
source, core, output = map(pathlib.Path, (source_arg, core_arg, output_arg))
if not core_arg:
    raise SystemExit("Integration requires IISDKBUILDER_CORE_SOURCE_DIR")
if output.exists():
    shutil.rmtree(output)
output.mkdir(parents=True, exist_ok=True)
app = output / "sources" / "app with spaces 한글"
addon = output / "sources" / "addon with spaces"
for origin, target in ((source / "tests/fixtures/app", app), (source / "tests/fixtures/addon", addon)):
    shutil.copytree(origin, target, dirs_exist_ok=True)
host = {"darwin": "macos", "win32": "windows", "linux": "linux"}[sys.platform]
manifest = output / "iiSDKBuilder.json"
data = {
    "schema": 1,
    "app": {"name": "Hello", "source": str(app), "targets": ["hello"]},
    "sdkRoot": str(core.parent),
    "sdks": [{"name": "Addon", "source": str(addon), "depends": ["iiSDKCore"]}],
    "buildRoot": str(output / "matrix"),
    "cmake": cmake,
    "jobs": 2,
    "parallelKits": 2,
    "kits": [{"id": "native", "platform": host}],
}

def run(action, expected=0):
    manifest.write_text(json.dumps(data), encoding="utf-8")
    result = subprocess.run([tool, action, "--manifest", str(manifest)], capture_output=True, text=True, timeout=240)
    if result.returncode != expected:
        raise AssertionError(f"{action}: expected {expected}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result

plan = json.loads(run("plan").stdout)
assert not (output / "matrix").exists(), "plan must not create a build tree on first run"
assert [p["name"] for p in plan["kits"][0]["projects"]] == ["iiSDKCore", "Addon", "Hello"]
report = json.loads(run("build").stdout)
assert report["succeeded"] and len(report["kits"][0]["artifacts"]) == 1
executable = pathlib.Path(report["kits"][0]["artifacts"][0])
assert executable.is_file()
native_env=os.environ.copy()
native_env["PATH"]=""
for name in ("PYTHONHOME","PYTHONPATH","NODE_PATH","CLASSPATH","DOTNET_ROOT","LD_LIBRARY_PATH","DYLD_LIBRARY_PATH","DYLD_FALLBACK_LIBRARY_PATH"):
    native_env.pop(name,None)
result = subprocess.run([str(executable)], env=native_env, capture_output=True, text=True, timeout=15)
assert result.returncode == 0 and "iiSDKBuilder native app: 0" in result.stdout
assert report["kits"][0]["executionModel"] == "native"
assert len(report["kits"][0]["verifiedArtifacts"]) == 1
assert report["kits"][0]["verifiedArtifacts"][0]["executionModel"] == "native"
assert report["kits"][0]["verifiedArtifacts"][0]["architectures"]
assert all(step["status"] == "succeeded" for step in report["kits"][0]["steps"])
assert len(report["kits"][0]["steps"]) == 8

# A present toolchain file must not silently turn a foreign kit into a host build.
toolchain = output / "wrong-platform.cmake"
toolchain.write_text("# Intentionally empty toolchain: leaves the host system selected.\n", encoding="utf-8")
foreign = "linux" if host == "windows" else "windows"
data["kits"].append({"id": "mismatched", "platform": foreign, "toolchain": str(toolchain)})
partial = json.loads(run("build", 1).stdout)
assert not partial["succeeded"]
assert partial["kits"][0]["status"] == "succeeded", "independent native kit continues"
assert partial["kits"][1]["status"] == "failed"
failed = next(step for step in partial["kits"][1]["steps"] if step["status"] == "failed")
assert "iiSDKBuilder expected" in pathlib.Path(failed["log"]).read_text(encoding="utf-8")
persisted = json.loads(pathlib.Path(partial["report"]).read_text(encoding="utf-8"))
assert persisted == partial
assert not (output / "matrix/.iisdkbuilder.lock").exists()

# The application can invoke the host builder through a regular CMake target.
wrapper = output / "cmake-entry"
wrapper.mkdir(exist_ok=True)
def bracket(value):
    return "[==[" + str(value).replace("\\", "/") + "]==]"
(wrapper / "CMakeLists.txt").write_text(
    "cmake_minimum_required(VERSION 3.31)\nproject(MatrixEntry NONE)\n"
    + "include(" + bracket(source / "cmake/iiSDKBuilderMatrix.cmake") + ")\n"
    + "iisdkbuilder_add_matrix(TARGET matrix MANIFEST " + bracket(manifest)
    + " BUILDER " + bracket(tool) + " PLATFORMS host)\n", encoding="utf-8")
for command in ([cmake, "-S", str(wrapper), "-B", str(wrapper / "build")],
                [cmake, "--build", str(wrapper / "build"), "--target", "matrix"]):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
print("Real SDK/addon/app build, app execution, incremental reuse, wrong-target rejection and CMake target: passed")
