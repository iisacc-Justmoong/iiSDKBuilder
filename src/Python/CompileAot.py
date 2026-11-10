#!/usr/bin/env python3
"""Compile one CPython module to a native extension using a pinned backend."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nuitka-launcher", required=True, type=Path)
    args = parser.parse_args()
    if sys.implementation.name != "cpython" or not ((3, 9) <= sys.version_info[:2] <= (3, 13)):
        parser.error("Nuitka 2.8.9 requires CPython 3.9 through 3.13")
    if not args.source.is_file() or args.source.suffix != ".py" or not args.source.stem.isidentifier():
        parser.error("source must be an existing Python module with a valid identifier")
    if not args.nuitka_launcher.is_file():
        parser.error("Nuitka launcher does not exist")
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    if not suffix or args.output.name != args.source.stem + suffix:
        parser.error("output must use the module name and the selected CPython extension suffix")
    args.source = args.source.resolve()
    args.output = args.output.resolve()
    args.nuitka_launcher = args.nuitka_launcher.resolve()
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["NUITKA_UPDATE_CHECK"] = "never"
    include = sysconfig.get_config_var("INCLUDEPY")
    if include and Path(include).is_dir():
        env["CPATH"] = include
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cpython-aot-", dir=args.output.parent) as temporary:
        env["NUITKA_CACHE_DIR"] = str(Path(temporary) / "cache")
        env["TMPDIR"] = temporary
        env["TEMP"] = temporary
        env["TMP"] = temporary
        version = subprocess.run([sys.executable, "-B", str(args.nuitka_launcher), "--version"],
                                 check=True, capture_output=True, text=True, env=env, cwd=temporary).stdout.splitlines()
        if not version or version[0].strip() != "2.8.9":
            parser.error("the approved Apache-2.0 compiler backend is Nuitka 2.8.9")
        command = [sys.executable, "-B", str(args.nuitka_launcher), "--mode=module", "--remove-output",
                   "--disable-cache=all", "--no-pyi-file", "--jobs=2", "--output-dir=" + temporary]
        if sys.platform == "darwin":
            command.append("--lto=no")
        subprocess.run([*command, str(args.source)], check=True, env=env, cwd=temporary)
        artifact = Path(temporary) / args.output.name
        if not artifact.is_file():
            raise RuntimeError("Nuitka did not produce the requested CPython extension")
        with artifact.open("rb") as stream:
            magic = stream.read(4)
        valid = (magic == b"\x7fELF" or magic[:2] == b"MZ" or magic in
                 (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"))
        if not valid:
            raise RuntimeError("AOT output is not a recognized native image")
        os.replace(artifact, args.output)


if __name__ == "__main__":
    main()
