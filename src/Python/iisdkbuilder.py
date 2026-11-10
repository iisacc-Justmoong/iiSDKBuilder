"""Python client for the native iiSDKBuilder CLI.

The Python process is a development client. Built applications do not import
this module or need a Python runtime.
"""
import json
import os
import subprocess


class BuilderError(RuntimeError):
    def __init__(self, message, report=None):
        super().__init__(message)
        self.report = report


class Builder:
    def __init__(self, executable="iisdk-builder", timeout=1800):
        self.executable = os.fspath(executable)
        self.timeout = timeout

    def _invoke(self, action, manifest):
        try:
            result = subprocess.run([self.executable, action, "--manifest", os.fspath(manifest)],
                                    capture_output=True, text=True, timeout=self.timeout, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BuilderError(str(error)) from error
        try:
            report = json.loads(result.stdout) if result.stdout.strip() else None
        except ValueError as error:
            raise BuilderError("Builder returned invalid JSON: " + result.stderr.strip()) from error
        if result.returncode:
            raise BuilderError(result.stderr.strip() or f"Builder failed with exit code {result.returncode}", report)
        if not isinstance(report, dict):
            raise BuilderError("Builder returned no JSON result")
        return report

    def plan(self, manifest):
        return self._invoke("plan", manifest)

    def build(self, manifest):
        return self._invoke("build", manifest)
