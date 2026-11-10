"""Contract tests for isolated, cleaned builder test workspaces."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("workspace_runner", ROOT / "tests" / "workspace_runner.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class WorkspaceRunnerTests(unittest.TestCase):
    def test_each_invocation_uses_and_removes_its_own_resources_directory(self):
        seen = []
        def execute(args, **kwargs):
            workspace = Path(kwargs["cwd"])
            self.assertTrue(workspace.is_dir())
            self.assertTrue(workspace.is_relative_to(ROOT / "resources"))
            self.assertEqual(workspace.name, "build")
            self.assertEqual(args[0], str(workspace / "file"))
            self.assertEqual(kwargs["env"]["TMPDIR"], str(workspace))
            seen.append(workspace)
            return subprocess.CompletedProcess(args, 0)
        self.assertEqual(runner.run(ROOT / "resources", ["@WORK@/file"], execute=execute), 0)
        self.assertFalse(seen[0].parent.exists())
        self.assertEqual(runner.run(ROOT / "resources", ["@WORK@/file"], execute=execute), 0)
        self.assertNotEqual(seen[0], seen[1])
        self.assertFalse(seen[1].parent.exists())

    def test_failed_or_interrupted_operation_removes_its_workspace(self):
        seen = []
        def execute(args, **kwargs):
            seen.append(Path(kwargs["cwd"]))
            raise RuntimeError("expected fixture failure")
        with self.assertRaises(RuntimeError):
            runner.run(ROOT / "resources", ["fixture"], execute=execute)
        self.assertFalse(seen[0].parent.exists())

if __name__ == "__main__":
    unittest.main()
