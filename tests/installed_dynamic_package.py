"""Exercise a relocated Builder installation without executing its fixture app."""
from pathlib import Path
import shutil
import subprocess
import sys

cmake, build, source, work, python, tool_name = sys.argv[1:]
work = Path(work)
stage = work / 'stage'
relocated = work / 'relocated'
subprocess.run([cmake, '--install', build, '--config', 'Release', '--prefix', str(stage)], check=True, capture_output=True, text=True)
shutil.move(str(stage), str(relocated))
script = relocated / 'lib' / 'cmake' / 'iiSDKBuilder' / 'iiSDKBuilderPackageRuntime.cmake'
assert script.is_file(), 'installed packager missing'
subprocess.run([python, '-B', str(Path(source) / 'tests' / 'dynamic_package.py'),
                str(relocated / 'bin' / tool_name), str(work / 'relocated-check'), cmake], check=True)
