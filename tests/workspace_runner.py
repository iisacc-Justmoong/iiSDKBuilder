"""Run one builder test in a unique, automatically removed resource workspace."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import uuid


def run(resources, command, *, execute=subprocess.run):
    import ii_file_provider as provider
    resources = Path(resources).resolve()
    state = resources / ".state"
    provider.mkdir(state, parents=True, exist_ok=True)
    owner = state / ("t-" + uuid.uuid4().hex[:16])
    provider.mkdir(owner, exist_ok=False)
    try:
        workspace = owner / "build"
        provider.mkdir(workspace, exist_ok=False)
        expanded = [part.replace("@WORK@", str(workspace)) for part in command]
        env = dict(os.environ, TMPDIR=str(workspace), TMP=str(workspace), TEMP=str(workspace))
        return execute(expanded, cwd=workspace, env=env, check=False).returncode
    finally:
        shutil.rmtree(owner)


def main(argv):
    if len(argv) < 3:
        raise ValueError("expected provider directory, resource directory and command")
    sys.path.insert(0, argv[0])
    return run(argv[1], argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
