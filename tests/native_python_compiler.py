"""Check the interpreter-free scalar Python plugin compiler contract."""
from pathlib import Path
import subprocess
import sys

root, work = map(Path, sys.argv[1:])
compiler = root / "src/Python/CompileStaticPlugin.py"
work.mkdir(parents=True, exist_ok=True)
source = work / "twice.py"

def compile_source(text):
    source.write_text(text, encoding="utf-8")
    return subprocess.run([sys.executable, "-B", str(compiler), "--source", str(source)],
                          capture_output=True, text=True)

result=compile_source("def transform(value: int) -> int:\n    return value * 2 + 3\n")
assert result.returncode==0, result.stderr
assert '"twice"' in result.stdout and "ii_sdk_plugin_v1" in result.stdout
assert "Py_Initialize" not in result.stdout and "Python.h" not in result.stdout
for invalid in (
    "import os\ndef transform(value: int) -> int:\n    return value\n",
    "def transform(value: int) -> int:\n    return eval('value')\n",
    "def transform(value: int) -> int:\n    return value ** 3\n",
    "def transform(value):\n    return value\n",
    "def first(value: int) -> int:\n    return value\ndef second(value: int) -> int:\n    return value\n",
    "def transform(value: int) -> int:\n    return value + 9223372036854775808\n",
):
    result=compile_source(invalid)
    assert result.returncode!=0 and result.stderr, f"unsupported Python silently accepted: {invalid}"
print("interpreter-free Python source contract passed")
