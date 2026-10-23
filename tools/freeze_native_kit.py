#!/usr/bin/env python3
"""Freeze or verify the installed headers, packages and binaries of a native kit."""
import argparse
import hashlib
import json
import platform
import re
import os
from pathlib import Path


def inventory(prefix):
    files = {}
    for directory in ("include", "lib", "bin", "share/licenses"):
        root = prefix / directory
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            relative = path.relative_to(prefix).as_posix()
            if path.is_symlink():
                link=os.readlink(path)
                try:
                    target=path.resolve(strict=True)
                except OSError as error:
                    raise ValueError("Broken native kit symlink: " + relative) from error
                if Path(link).is_absolute() or not target.is_relative_to(prefix):
                    raise ValueError("Non-relocatable native kit symlink: " + relative)
                files[relative]={"symlink":link}
                continue
            if not path.is_file():
                continue
            if path.suffix in {".h", ".hpp", ".cmake"}:
                source = path.read_text(errors="replace")
                if re.search(r'^\s*#\s*include\s*[<"](?:Qt|Q[A-Z])', source, re.M):
                    raise ValueError("Qt header in the native kit: " + relative)
                if re.search(r'\bfind_(?:package|dependency)\s*\(\s*Qt[56]?\b', source):
                    raise ValueError("Qt package in the native kit: " + relative)
            digest = hashlib.sha256()
            with path.open("rb") as handle:
                for block in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(block)
            files[relative] = {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}
    if not files:
        raise ValueError("The native kit is empty")
    return files


def freeze(prefix, check=False):
    prefix = prefix.resolve()
    if "build" not in prefix.parts:
        raise ValueError("Keep the native staging prefix inside build/")
    manifest = prefix / "native-kit-manifest.json"
    files = inventory(prefix)
    if check:
        expected = json.loads(manifest.read_text())
        if expected.get("files") != files:
            changed = sorted(key for key in set(expected.get("files", {})) | set(files)
                             if expected.get("files", {}).get(key) != files.get(key))
            raise ValueError("Native kit changed after freezing: " + ", ".join(changed[:12]))
    else:
        manifest.write_text(json.dumps({"schema": 1, "system": platform.system(),
            "machine": platform.machine(), "qt_public_api": False, "files": files},
            indent=2, sort_keys=True) + "\n")
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    print("NATIVE_KIT_OK", freeze(args.prefix, args.check), "installed files")


if __name__ == "__main__":
    main()
