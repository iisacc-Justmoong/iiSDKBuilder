"""Exercise browser module packaging without launching the generated application."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
cmake = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='wasm-module-package-', dir=root / 'resources') as temporary:
    work = Path(temporary)
    script = root / 'cmake/iiSDKBuilderPackageWasm.cmake'
    for extension in ('mjs', 'js', 'html'):
        source = work / extension
        source.mkdir()
        stem = 'my.app'
        worker = f'{stem}.worker.mjs' if extension == 'mjs' else f'{stem}.worker.js'
        entry = source / f'{stem}.{extension}'
        loader = entry if extension != 'html' else source / f'{stem}.js'
        loader.write_text(f'{worker} my.app.data')
        if entry != loader:
            entry.write_text('<script src="my.app.js"></script>')
        wasm = source / f'{stem}.wasm'
        wasm.write_bytes(b'\0asm\1\0\0\0')
        (source / worker).write_text('// worker')
        (source / f'{stem}.data').write_bytes(b'data')
        destination = work / f'{extension}-package'
        subprocess.run([cmake, f'-DINPUT={entry}', f'-DMAIN_WASM={wasm}',
                        f'-DDESTINATION={destination}', '-P', str(script)],
                       check=True, timeout=30)
        expected = {entry.name, loader.name, wasm.name, worker, f'{stem}.data'}
        assert expected <= {p.name for p in destination.iterdir()}, extension
        if extension == 'mjs':
            (source / worker).unlink()
            missing = subprocess.run([cmake, f'-DINPUT={entry}', f'-DMAIN_WASM={wasm}',
                                      f'-DDESTINATION={work / "missing-worker"}', '-P', str(script)],
                                     capture_output=True, text=True, timeout=30)
            assert missing.returncode and worker in missing.stderr, 'missing ES module worker must fail'
print('Classic, ES module and HTML loaders packaged with their named assets')
