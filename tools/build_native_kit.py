#!/usr/bin/env python3
"""Build the desktop kit's pinned C dependencies without a package manager.

This is build/install tooling, not an application file factory. All downloads,
sources, build products, logs and staging stay below an explicit build/ root.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import subprocess
import tarfile
import urllib.request


def manifest():
    return json.loads(Path(__file__).with_name("native-kit-dependencies.json").read_text())


def digest(path):
    with path.open("rb") as source:
        result = hashlib.sha256()
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def unpack(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as bundle:
        for member in bundle.getmembers():
            path = PurePosixPath(member.name)
            if not path.parts or path.is_absolute() or ".." in path.parts or ":" in path.parts[0]:
                raise ValueError("Unsafe dependency archive member")
            if not (member.isfile() or member.isdir()):
                raise ValueError("Dependency archives must contain regular files and directories")
        # Reject path traversal, escaping links and special devices. Python's
        # data filter also validates link targets before anything is installed.
        if hasattr(tarfile, "data_filter"):
            bundle.extractall(destination, filter="data")
        else:
            # Python 3.9 on older Xcode has no extraction filters. Every member
            # above has been checked and no link or special file is permitted.
            bundle.extractall(destination)
    roots = [p for p in destination.iterdir() if p.is_dir()]
    if len(roots) != 1:
        raise ValueError("A dependency archive must contain one source root")
    return roots[0]


def configuration(name):
    return {
        "jpeg": ["ENABLE_SHARED=OFF", "ENABLE_STATIC=ON", "WITH_TURBOJPEG=OFF", "WITH_TOOLS=OFF", "WITH_TESTS=OFF"],
        "png": ["PNG_SHARED=OFF", "PNG_STATIC=ON", "PNG_FRAMEWORK=OFF", "PNG_TESTS=OFF", "PNG_TOOLS=OFF", "PNG_EXECUTABLES=OFF"],
        "webp": ["BUILD_SHARED_LIBS=OFF", "WEBP_BUILD_ANIM_UTILS=OFF", "WEBP_BUILD_CWEBP=OFF", "WEBP_BUILD_DWEBP=OFF",
                 "WEBP_BUILD_VWEBP=OFF", "WEBP_BUILD_WEBPINFO=OFF", "WEBP_BUILD_WEBPMUX=OFF", "WEBP_BUILD_EXTRAS=OFF"],
        "tiff": ["BUILD_SHARED_LIBS=OFF", "tiff-tools=OFF", "tiff-tests=OFF", "tiff-contrib=OFF", "tiff-docs=OFF",
                 "jpeg=ON", "zlib=ON", "webp=ON", "libdeflate=OFF", "lzma=OFF", "zstd=OFF", "jbig=OFF", "lerc=OFF"],
    }[name]


def build(args):
    root, prefix = args.root.resolve(), args.prefix.resolve()
    if "build" not in root.parts or "build" not in prefix.parts:
        raise ValueError("Native-kit sources and prefix must remain inside build/")
    root.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("DYLD_") and k not in
           {"CPATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "LD_LIBRARY_PATH"}}
    if platform.system() == "Darwin":
        env["MACOSX_DEPLOYMENT_TARGET"] = args.minimum_macos
    records = []
    for dep in manifest()["dependencies"]:
        if args.only and dep["name"] not in args.only:
            continue
        work = root / (dep["name"] + "-" + dep["version"])
        work.mkdir(exist_ok=True)
        archive = root / dep["archive"]
        if not archive.exists() or digest(archive) != dep["sha256"]:
            download = archive.with_suffix(archive.suffix + ".download")
            print("Downloading", dep["name"], dep["version"], flush=True)
            with urllib.request.urlopen(dep["url"], timeout=120) as response, download.open("wb") as output:
                shutil.copyfileobj(response, output, length=1024 * 1024)
            if digest(download) != dep["sha256"]:
                raise ValueError("Archive checksum mismatch: " + dep["name"])
            download.replace(archive)
        source_dir = work / "source"
        if not source_dir.exists() or not any(source_dir.iterdir()):
            source = unpack(archive, source_dir)
        else:
            source = next(p for p in source_dir.iterdir() if p.is_dir())
        target = work / "build"
        target.mkdir(exist_ok=True)
        log_path = work / "build.log"
        with log_path.open("w") as log:
            def run(command):
                log.write("COMMAND " + repr([str(x) for x in command]) + "\n"); log.flush()
                result = subprocess.run([str(x) for x in command], cwd=target, env=env, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError(f"{dep['name']} failed ({result.returncode}): {log_path}")
            if dep["name"] == "openssl":
                if platform.system() == "Darwin":
                    triple = "darwin64-arm64-cc" if platform.machine() == "arm64" else "darwin64-x86_64-cc"
                elif platform.system() == "Linux":
                    triple = "linux-aarch64" if platform.machine() in ("arm64", "aarch64") else "linux-x86_64"
                else:
                    raise ValueError("Use the Windows native OpenSSL recipe on MSVC")
                run(["perl", source / "Configure", triple, "no-shared", "no-module", "no-tests", "-fPIC",
                     "--prefix=" + str(prefix), "--libdir=lib"])
                run(["make", "-j" + str(args.parallel)])
                run(["make", "install_sw"])
            else:
                command = ["cmake", "-S", source, "-B", target, "-DCMAKE_BUILD_TYPE=Release",
                           "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                           "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_PREFIX_PATH=" + str(prefix), "-DBUILD_TESTING=OFF"]
                if shutil.which("ninja"):
                    command += ["-G", "Ninja"]
                if platform.system() == "Darwin":
                    command += ["-DCMAKE_OSX_DEPLOYMENT_TARGET=" + args.minimum_macos,
                                "-DCMAKE_OSX_ARCHITECTURES=" + platform.machine()]
                command += ["-D" + option for option in configuration(dep["name"])]
                run(command)
                run(["cmake", "--build", target, "--parallel", str(args.parallel)])
                run(["cmake", "--install", target])
        # Redistribution notices travel with the staged SDK kit.
        notices = prefix / "share" / "licenses" / dep["name"]
        notices.mkdir(parents=True, exist_ok=True)
        for name in ("LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "README.ijg"):
            if (source / name).is_file():
                shutil.copy2(source / name, notices / name)
        record = dict(dep, minimum_macos=args.minimum_macos if platform.system() == "Darwin" else None,
                      architecture=platform.machine(), linkage="static", log=str(log_path))
        records.append(record)
        (work / "result.json").write_text(json.dumps(record, indent=2) + "\n")
        print("Built", dep["name"], dep["version"], flush=True)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "build" / "native-kit")
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--minimum-macos", default="12.0")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--only", nargs="+")
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error("parallel must be positive")
    build(args)


if __name__ == "__main__":
    main()
