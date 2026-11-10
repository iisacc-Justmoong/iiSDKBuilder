"""A timed-out builder is asked to cancel its owned processes before cleanup."""
from pathlib import Path
import subprocess
import sys
import tempfile

from builder_subprocess import run_builder

with tempfile.TemporaryDirectory(dir=sys.argv[1]) as work:
    marker = Path(work) / 'cancelled'
    source = '''import signal, sys, time
from pathlib import Path
def stop(*args):
    Path(sys.argv[1]).write_text('cancelled')
    raise SystemExit(0)
signal.signal(signal.SIGTERM, stop)
print('ready', flush=True)
time.sleep(60)
'''
    try:
        run_builder([sys.executable, '-c', source, str(marker)], timeout=2)
        raise AssertionError('A timed-out command was reported as successful')
    except subprocess.TimeoutExpired:
        assert marker.read_text() == 'cancelled', 'The child did not receive graceful cancellation'
print('Builder timeout requests cancellation before workspace cleanup')
