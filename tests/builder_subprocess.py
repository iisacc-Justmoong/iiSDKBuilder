"""Run a builder fixture with a deadline and cooperative timeout cleanup."""
import subprocess


def run_builder(arguments, *, timeout):
    process = subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        # On POSIX the builder handles SIGTERM by cancelling and joining its
        # owned process tree. An immediate kill races workspace cleanup.
        try:
            process.terminate()
        except ProcessLookupError:
            pass  # It completed between the deadline and cancellation.
        try:
            stdout, stderr = process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
        raise subprocess.TimeoutExpired(arguments, timeout, output=stdout, stderr=stderr) from None
    return subprocess.CompletedProcess(arguments, process.returncode, stdout, stderr)
