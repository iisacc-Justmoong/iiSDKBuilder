import os
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / "src/Python/CompileAot.py"
SOURCE = ROOT / "resources/cpython-aot/iixml_aot_probe.py"
REJECTED = ROOT / "resources/cpython-aot/rejected_compiler.py"
STATE = ROOT / "resources/.state"


class BackendFailures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        STATE.mkdir(parents=True, exist_ok=True)

    def test_rejected_compiler_preserves_previous_artifact(self):
        with tempfile.TemporaryDirectory(prefix="cpython-aot-test-", dir=STATE) as temporary:
            output = Path(temporary) / (SOURCE.stem + sysconfig.get_config_var("EXT_SUFFIX"))
            output.write_bytes(b"previous artifact")
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
            result = subprocess.run([sys.executable, "-B", str(BACKEND), "--source", str(SOURCE),
                                     "--output", str(output), "--nuitka-launcher", str(REJECTED)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Nuitka 2.8.9", result.stderr)
            self.assertEqual(output.read_bytes(), b"previous artifact")
            self.assertEqual(sorted(p.name for p in Path(temporary).iterdir()), [output.name])

    def test_invalid_output_name_rejected(self):
        with tempfile.TemporaryDirectory(prefix="cpython-aot-test-", dir=STATE) as temporary:
            output = Path(temporary) / "different.so"
            result = subprocess.run([sys.executable, "-B", str(BACKEND), "--source", str(SOURCE),
                                     "--output", str(output), "--nuitka-launcher", str(REJECTED)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("module name", result.stderr)
            self.assertFalse(output.exists())

    def test_mismatched_core_abi_rejected_during_configuration(self):
        with tempfile.TemporaryDirectory(prefix="cpython-abi-test-", dir=ROOT / "build") as temporary:
            result = subprocess.run([
                shutil.which("cmake") or "cmake", "-S", str(ROOT / "tests/cpython_abi"),
                "-B", str(Path(temporary) / "build"),
                "-DIISDK_HELPER=" + str(ROOT / "cmake/iiSDKBuilderCPython.cmake"),
                "-DIISDK_PYTHON=" + sys.executable, "-DIISDK_LAUNCHER=" + str(REJECTED),
                "-DIISDK_SOURCE=" + str(SOURCE),
            ], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CPython ABI", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
