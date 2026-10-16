"""Build native embedded assets through Builder; prove isolation and incremental dependencies."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

tool, source_arg, core_arg, output_arg, cmake = sys.argv[1:]
source, core, output = map(Path, (source_arg, core_arg, output_arg))
output = output / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
output.mkdir(parents=True)
app = output / "source app 한글"
shutil.copytree(source / "examples/HelloResources", app)
host = {"darwin": "macos", "win32": "windows", "linux": "linux"}[sys.platform]
environment = os.environ.copy()
for key in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LIBRARY_PATH"):
    environment.pop(key, None)
steps = []

def run(name, command, env=environment):
    result = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True, timeout=240)
    log = output / (name + ".log")
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    steps.append({"step": name, "arguments": list(map(str, command)), "exitCode": result.returncode, "log": str(log)})
    if result.returncode:
        raise AssertionError(f"{name}: {log}\n{result.stdout[-4000:]}\n{result.stderr[-4000:]}")
    return result.stdout

manifest = output / "iiSDKBuilder.json"
manifest.write_text(json.dumps({
    "schema": 1,
    "app": {"name": "HelloResources", "source": str(app), "targets": ["HelloResources"], "cache": {
        "IISDKBUILDER_RESOURCE_COMPILER": str(Path(tool).resolve()),
        "IISDKBUILDER_RESOURCES_MODULE": str(source / "cmake/iiSDKBuilderResources.cmake"),
        "BUILD_TESTING": "ON", "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"}},
    "sdkRoot": str(core.parent), "buildRoot": str(output / "matrix"),
    "cmake": cmake, "jobs": 2, "parallelKits": 1,
    "kits": [{"id": "native", "platform": host}]
}), encoding="utf-8")
report = json.loads(run("builder", [tool, "build", "--manifest", manifest]))
kit = report["kits"][0]
assert report["succeeded"] and len(kit["steps"]) == 5
assert all(s["status"] == "succeeded" for s in kit["steps"])
assert kit["verifiedArtifacts"][0]["executionModel"] == "native"
executable = Path(kit["verifiedArtifacts"][0]["file"])
app_build = output / "matrix/kits/native/Release/app/build"
pack = next((app_build / "resources").glob("*/*.iirp"))
native_env = environment.copy()
native_env["PATH"] = ""
for key in ("PYTHONHOME", "PYTHONPATH", "NODE_PATH", "CLASSPATH", "DOTNET_ROOT",
            "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "LD_LIBRARY_PATH"):
    native_env.pop(key, None)
shutil.move(app / "assets", app / "assets.removed")
result = run("without-source-assets", [executable], native_env)
assert "NATIVE_RESOURCES_OK embedded=true deduplicated=true entries=4" in result and f"target={host}" in result
shutil.move(app / "assets.removed", app / "assets")
before = hashlib.sha256(pack.read_bytes()).hexdigest()
greeting = app / "assets/greeting.txt"
greeting.write_text(greeting.read_text(encoding="utf-8") + "Incremental resource update.\n", encoding="utf-8")
rebuilt = run("changed-asset", [cmake, "--build", app_build, "--parallel", "2"])
assert "Compiling native Core resources" in rebuilt
assert before != hashlib.sha256(pack.read_bytes()).hexdigest()
assert "Incremental resource update." in run("changed-asset-execution", [executable], native_env)
unchanged = run("unchanged", [cmake, "--build", app_build])
assert "RESOURCE_BUILD_OK" not in unchanged, "unchanged dependencies must not recompile resources"

# Manifest changes reconfigure automatically; Make-style depfile escaping must
# survive spaces, non-ASCII text, # and $ through the actual build tool.
extra = app / "assets/dep file #$ 한글.txt"
extra.write_text("first dependency", encoding="utf-8")
resources = app / "resources.json"
data = json.loads(resources.read_text(encoding="utf-8"))
data["resources"].append({"name": "extra", "source": "assets/" + extra.name})
resources.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
run("changed-manifest", [cmake, "--build", app_build])
assert "entries=5" in run("manifest-execution", [executable], native_env)
before = hashlib.sha256(pack.read_bytes()).hexdigest()
extra.write_text("changed dependency", encoding="utf-8")
run("escaped-dependency", [cmake, "--build", app_build])
assert before != hashlib.sha256(pack.read_bytes()).hexdigest()
evidence = {"status": "passed", "target": host, "builderReport": report["report"],
            "executable": str(executable), "steps": steps,
            "sourceAssetsAbsentDuringExecution": True, "incrementalAssetAndManifest": True,
            "escapedDependencyRebuilt": True, "unchangedSkippedCompiler": True}
(output / "verification.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
print("Native resources: Builder chain, source-free execution, incremental changes and escaped dependencies passed")
print(output / "verification.json")
