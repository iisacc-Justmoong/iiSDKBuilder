#!/usr/bin/env python3
"""Configure, build, and test one iiSDK C++ repository on a GitHub-hosted runner.

The workflow invokes this file on both ubuntu-latest and windows-latest. Dependency
repositories are cloned, built, and installed under $HOME/.local/SDK/<module>
before the caller project is configured.
"""

from __future__ import annotations

import os
import platform
import re
import shlex
import shutil
import site
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Dependency:
    owner: str
    repo: str
    ref: str
    cmake_args: list[str] = field(default_factory=list)

    @property
    def name(self) -> str:
        return self.repo


# Used when the project being built is iiSDKBuilder and the caller did not
# supply its own dependency list. Qt stays off; SQLite is compiled from the
# pinned source so neither OS needs a system SQLite SDK.
BUILDER_DEPENDENCIES = """
iisacc-Justmoong/iiSDKCore@master -- -DBUILD_TESTING=OFF -DIISDKCORE_BUILD_EXAMPLES=OFF
iisacc-Justmoong/iiFileProvider@master -- -DBUILD_TESTING=OFF -DIIFILEPROVIDER_BUILD_QT_API=OFF -DIIFILEPROVIDER_USE_BUNDLED_SQLITE=ON -DIIFILEPROVIDER_BUILD_TOOLS=OFF
"""
BUILDER_CMAKE_ARGS = "-DIISDKBUILDER_CORE_SOURCE_DIR=@dep:iiSDKCore@"
BUILDER_APT: list[str] = []
BUILDER_CHOCO = ["llvm"]


def log(message: str) -> None:
    print(message, flush=True)


def remove_tree(path: Path) -> None:
    def onerror(func, file, _exc):
        os.chmod(file, 0o700)
        func(file)

    if path.exists():
        shutil.rmtree(path, onerror=onerror)


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    log("+ " + " ".join(command))
    subprocess.run(command, cwd=cwd, env=env, check=True)


def capture(command: list[str]) -> str:
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def windows() -> bool:
    return os.environ.get("RUNNER_OS", platform.system()) == "Windows"


def home_directory() -> Path:
    home = os.environ.get("HOME") or os.environ.get("USERPROFILE")
    if not home:
        raise SystemExit("HOME and USERPROFILE are unset")
    os.environ["HOME"] = home
    return Path(home)


def prepend_path(directory: Path) -> None:
    if directory.is_dir():
        os.environ["PATH"] = str(directory) + os.pathsep + os.environ.get("PATH", "")


def cmake_version(text: str) -> tuple[int, int, int]:
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", text)
    if not match:
        raise SystemExit(f"Could not parse cmake version from: {text}")
    return tuple(int(part) for part in match.groups())


def install_cmake() -> None:
    current = shutil.which("cmake")
    if current:
        try:
            if cmake_version(capture([current, "--version"])) >= (3, 31, 0):
                log(capture([current, "--version"]).splitlines()[0])
                return
        except (OSError, subprocess.CalledProcessError):
            pass
    pip = subprocess.run([sys.executable, "-m", "pip", "install", "--user", "cmake>=3.31,<4"])
    scripts = Path(site.getuserbase()) / ("Scripts" if os.name == "nt" else "bin")
    prepend_path(scripts)
    if pip.returncode != 0 or shutil.which("cmake") is None or cmake_version(capture(["cmake", "--version"])) < (3, 31, 0):
        install_cmake_binary()
    version = capture(["cmake", "--version"])
    if cmake_version(version) < (3, 31, 0):
        raise SystemExit(version)
    log(version.splitlines()[0])


def install_cmake_binary() -> None:
    version = "3.31.10"
    tools = Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "cmake-kit"
    tools.mkdir(parents=True, exist_ok=True)
    if windows():
        name = f"cmake-{version}-windows-x86_64"
        archive = tools / f"{name}.zip"
        url = f"https://github.com/Kitware/CMake/releases/download/v{version}/{name}.zip"
        urllib.request.urlretrieve(url, archive)
        with zipfile.ZipFile(archive) as package:
            package.extractall(tools)
        prepend_path(tools / name / "bin")
    else:
        name = f"cmake-{version}-linux-x86_64"
        archive = tools / f"{name}.tar.gz"
        url = f"https://github.com/Kitware/CMake/releases/download/v{version}/{name}.tar.gz"
        urllib.request.urlretrieve(url, archive)
        with tarfile.open(archive) as package:
            package.extractall(tools)
        prepend_path(tools / name / "bin")


def expose_clang_scan_deps() -> None:
    """Put an unversioned clang-scan-deps on PATH.

    Ubuntu installs clang-scan-deps-18 or /usr/lib/llvm-*/bin/clang-scan-deps.
    CMake looks for the unversioned name beside the compiler and on PATH.
    """
    if shutil.which("clang-scan-deps"):
        return
    candidates = sorted(Path("/usr/lib").glob("llvm-*/bin/clang-scan-deps"))
    candidates.extend(sorted(Path("/usr/bin").glob("clang-scan-deps-*")))
    if not candidates:
        return
    bindir = Path(site.getuserbase()) / "bin"
    bindir.mkdir(parents=True, exist_ok=True)
    link = bindir / "clang-scan-deps"
    if not link.exists():
        link.symlink_to(candidates[-1])
    prepend_path(bindir)


def install_linux_packages(packages: list[str]) -> None:
    # GCC is the default compiler. Clang and clang-scan-deps stay installed so a
    # caller can select Clang for C++ module scanning.
    needed = ["ninja-build", "ccache", "ca-certificates", "git", "python3", "g++", "clang", "clang-tools", *packages]
    unique: list[str] = []
    for package in needed:
        if package and package not in unique:
            unique.append(package)
    env = os.environ.copy()
    env["DEBIAN_FRONTEND"] = "noninteractive"
    run(["sudo", "apt-get", "update"], env=env)
    run(["sudo", "apt-get", "install", "-y", *unique], env=env)
    if not shutil.which("clang-scan-deps"):
        # Ubuntu splits the scanner out of the clang package on some releases.
        for package in ("clang-tools", "clang-tools-18", "clang-tools-19", "clang-tools-20", "clang-tools-21"):
            probe = subprocess.run(["sudo", "apt-get", "install", "-y", package], env=env)
            if probe.returncode == 0 and shutil.which("clang-scan-deps"):
                break
    expose_clang_scan_deps()


def install_windows_packages(packages: list[str]) -> None:
    needed = ["ninja", "sccache", *packages]
    unique = [package for package in needed if package]
    run(["choco", "install", *unique, "-y", "--no-progress"])
    prepend_path(Path(r"C:\Program Files\LLVM\bin"))
    prepend_path(Path(r"C:\ProgramData\chocolatey\bin"))


def activate_msvc() -> None:
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    install = capture(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ]
    ).strip().splitlines()[-1]
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        raise SystemExit(f"MSVC developer prompt is missing: {vcvars}")
    output = subprocess.check_output(f'call "{vcvars}" >nul && set', shell=True, text=True)
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key:
            os.environ[key] = value
    # `cl` with no input writes its banner to stderr and exits non-zero.
    banner = subprocess.run(["cl"], capture_output=True, text=True, timeout=30)
    first = (banner.stderr or banner.stdout).splitlines()
    if first:
        log(first[0])
    else:
        log("MSVC developer environment is active")


def split_specs(text: str) -> list[str]:
    parts: list[str] = []
    for line in text.replace(" || ", "\n").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            parts.append(stripped)
    return parts


def parse_dependencies(text: str, default_owner: str = "iisacc-Justmoong") -> list[Dependency]:
    dependencies: list[Dependency] = []
    for spec in split_specs(text):
        recipe, _, argument_text = spec.partition(" -- ")
        tokens = recipe.split()
        if len(tokens) != 1:
            raise SystemExit(f"Dependency must be owner/repo[@ref], not: {spec}")
        slug = tokens[0]
        if slug.startswith("https://") or slug.startswith("git@"):
            raise SystemExit(f"Use owner/repo[@ref], not a URL: {slug}")
        owner_repo, _, ref = slug.partition("@")
        if "/" in owner_repo:
            owner, _, repo = owner_repo.partition("/")
        else:
            owner, repo = default_owner, owner_repo
        repo = repo.removesuffix(".git")
        if not owner or not repo or "/" in repo:
            raise SystemExit(f"Invalid dependency: {spec}")
        arguments = shlex.split(argument_text, posix=True) if argument_text.strip() else []
        dependencies.append(Dependency(owner=owner, repo=repo, ref=ref, cmake_args=arguments))
    return dependencies


def is_builder_project(project: Path) -> bool:
    cmake = project / "CMakeLists.txt"
    if not cmake.is_file():
        return False
    head = cmake.read_text(encoding="utf-8", errors="replace")[:4000]
    return bool(re.search(r"project\s*\(\s*iiSDKBuilder\b", head))


def link_directory(link: Path, target: Path) -> None:
    if link.exists():
        return
    link.parent.mkdir(parents=True, exist_ok=True)
    try:
        link.symlink_to(target, target_is_directory=True)
        return
    except OSError as error:
        log(f"symlink failed ({error}); trying a junction")
    if windows():
        completed = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)])
        if completed.returncode == 0:
            return
    log(f"Could not mirror {target} at {link}")


def compiler_launchers() -> list[str]:
    if windows():
        tool = "sccache" if shutil.which("sccache") else ""
    else:
        tool = "ccache" if shutil.which("ccache") else ""
    if not tool:
        return []
    return [f"-DCMAKE_C_COMPILER_LAUNCHER={tool}", f"-DCMAKE_CXX_COMPILER_LAUNCHER={tool}"]


def jobs() -> str:
    count = os.cpu_count() or 2
    return str(max(1, min(count, 4)))


def with_prefix_path(extra: list[str], prefixes: list[Path]) -> list[str]:
    prefix_value = ";".join(path.as_posix() for path in prefixes)
    if not prefix_value:
        return extra
    merged: list[str] = []
    replaced = False
    for argument in extra:
        if argument.startswith("-DCMAKE_PREFIX_PATH="):
            existing = argument.split("=", 1)[1]
            argument = f"-DCMAKE_PREFIX_PATH={prefix_value};{existing}" if existing else f"-DCMAKE_PREFIX_PATH={prefix_value}"
            replaced = True
        merged.append(argument)
    if not replaced:
        merged.append(f"-DCMAKE_PREFIX_PATH={prefix_value}")
    return merged


def configure(source: Path, build: Path, prefix: Path, prefixes: list[Path], extra: list[str]) -> None:
    command = [
        "cmake",
        "-S",
        str(source),
        "-B",
        str(build),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={os.environ.get('INPUT_BUILD_TYPE') or 'Release'}",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
        *compiler_launchers(),
    ]
    if not windows():
        command.extend(["-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++"])
    # Caller arguments come last so they can override the compiler or build type.
    # Dependency prefixes are merged into CMAKE_PREFIX_PATH either way.
    command.extend(with_prefix_path(extra, prefixes))
    run(command)


def build_and_install(source: Path, build: Path, prefix: Path, prefixes: list[Path], extra: list[str]) -> None:
    if not any(arg.startswith("-DBUILD_TESTING=") for arg in extra):
        extra = [*extra, "-DBUILD_TESTING=OFF"]
    configure(source, build, prefix, prefixes, extra)
    run(["cmake", "--build", str(build), "--parallel", jobs()])
    run(["cmake", "--install", str(build)])


def fetch_dependency(dependency: Dependency, checkout: Path) -> None:
    remove_tree(checkout)
    command = [
        "git",
        "clone",
        "--depth",
        "1",
        "--recurse-submodules",
        "--shallow-submodules",
    ]
    if dependency.ref:
        command.extend(["--branch", dependency.ref])
    command.extend(
        [
            f"https://github.com/{dependency.owner}/{dependency.repo}.git",
            str(checkout),
        ]
    )
    env = os.environ.copy()
    env["GIT_TERMINAL_PROMPT"] = "0"
    run(command, env=env)


def substitute_deps(arguments: str, sources: dict[str, Path]) -> list[str]:
    def replace(match: re.Match[str]) -> str:
        name = match.group(1)
        if name not in sources:
            raise SystemExit(f"@dep:{name}@ was used but {name} is not a fetched dependency")
        return sources[name].as_posix()

    expanded = re.sub(r"@dep:([A-Za-z0-9_.+-]+)@", replace, arguments)
    return shlex.split(expanded, posix=True)


def dll_directories(build: Path) -> list[str]:
    found: list[str] = []
    if not build.exists():
        return found
    for path in build.rglob("*.dll"):
        directory = str(path.parent)
        if directory not in found:
            found.append(directory)
    return found


def test_environment(build: Path, prefixes: list[Path]) -> dict[str, str]:
    env = os.environ.copy()
    if windows():
        directories = dll_directories(build)
        directories.extend(str(prefix / "bin") for prefix in prefixes if (prefix / "bin").is_dir())
        env["PATH"] = os.pathsep.join([*directories, env.get("PATH", "")])
    else:
        libraries = [str(prefix / "lib") for prefix in prefixes if (prefix / "lib").is_dir()]
        libraries.extend(str(prefix / "lib64") for prefix in prefixes if (prefix / "lib64").is_dir())
        if libraries:
            previous = env.get("LD_LIBRARY_PATH", "")
            env["LD_LIBRARY_PATH"] = ":".join([*libraries, *([previous] if previous else [])])
    prefix_value = ";".join(prefix.as_posix() for prefix in prefixes)
    if prefix_value:
        env["CMAKE_PREFIX_PATH"] = prefix_value
    return env


def self_check() -> None:
    parsed = parse_dependencies(BUILDER_DEPENDENCIES)
    assert [item.name for item in parsed] == ["iiSDKCore", "iiFileProvider"], parsed
    assert parsed[1].cmake_args[0] == "-DBUILD_TESTING=OFF"
    one_line = parse_dependencies(
        "iiSDKCore@master -- -DBUILD_TESTING=OFF || iiJson@main -- -DBUILD_TESTING=OFF"
    )
    assert [item.repo for item in one_line] == ["iiSDKCore", "iiJson"]
    assert one_line[0].owner == "iisacc-Justmoong"
    assert substitute_deps("-DROOT=@dep:iiSDKCore@", {"iiSDKCore": Path("/src/iiSDKCore")}) == [
        "-DROOT=/src/iiSDKCore"
    ]
    log("self-check passed")


def main() -> None:
    if "--self-check" in sys.argv:
        self_check()
        return
    project = Path(os.environ["PROJECT_DIR"]).resolve()
    if not (project / "CMakeLists.txt").is_file():
        raise SystemExit(f"No CMakeLists.txt in {project}")
    home = home_directory()
    sdk_root = home / ".local" / "SDK"
    sdk_root.mkdir(parents=True, exist_ok=True)
    builder = is_builder_project(project)
    dependencies_text = os.environ.get("INPUT_DEPENDENCIES", "")
    cmake_text = os.environ.get("INPUT_CMAKE_ARGS", "")
    apt = shlex.split(os.environ.get("INPUT_APT_PACKAGES", ""))
    choco = shlex.split(os.environ.get("INPUT_CHOCO_PACKAGES", ""))
    if builder and not dependencies_text.strip():
        dependencies_text = BUILDER_DEPENDENCIES
        log("iiSDKBuilder push/call without dependencies: fetching iiSDKCore and iiFileProvider")
    if builder and not cmake_text.strip():
        cmake_text = BUILDER_CMAKE_ARGS
    if builder:
        apt = [*BUILDER_APT, *apt]
        choco = [*BUILDER_CHOCO, *choco]
    build_tests = os.environ.get("INPUT_BUILD_TESTS", "true").lower() == "true"
    if windows():
        install_windows_packages(choco)
        activate_msvc()
    else:
        install_linux_packages(apt)
        if not shutil.which("g++"):
            raise SystemExit("g++ is required for the Linux hosted toolchain")
        if not shutil.which("clang++") or not shutil.which("clang-scan-deps"):
            raise SystemExit("clang++ and clang-scan-deps are required so callers can select Clang module scanning")
    install_cmake()
    if not shutil.which("ninja"):
        raise SystemExit("Ninja was not installed")
    log("ninja " + capture(["ninja", "--version"]).strip())
    if builder and not shutil.which("clang++"):
        raise SystemExit("clang++ is required to configure iiSDKBuilder; HLS discovery uses find_program(clang++)")

    dependencies = parse_dependencies(dependencies_text)
    prefixes: list[Path] = []
    sources: dict[str, Path] = {}
    checkout_root = project.parent
    for dependency in dependencies:
        log(f"Dependency {dependency.owner}/{dependency.repo}@{dependency.ref or 'default'}")
        checkout = checkout_root / dependency.name
        fetch_dependency(dependency, checkout)
        sources[dependency.name] = checkout
        prefix = sdk_root / dependency.name
        remove_tree(prefix)
        build = checkout / "build"
        build_and_install(checkout, build, prefix, prefixes, list(dependency.cmake_args))
        prefixes.append(prefix)
        mirror = (project / ".." / ".." / "SDK" / dependency.name).resolve()
        link_directory(mirror, prefix)

    build_type = os.environ.get("INPUT_BUILD_TYPE") or "Release"
    os.environ["INPUT_BUILD_TYPE"] = build_type
    testing = "ON" if build_tests else "OFF"
    extra = substitute_deps(cmake_text, sources)
    if not any(arg.startswith("-DBUILD_TESTING=") for arg in extra):
        extra.append(f"-DBUILD_TESTING={testing}")
    build_dir = project / "build"
    configure(project, build_dir, sdk_root / project.name, prefixes, extra)
    run(["cmake", "--build", str(build_dir), "--parallel", jobs()])
    if not build_tests:
        log("BUILD_TESTING is off; ctest was not run")
        return
    junit = build_dir / "Testing" / "junit.xml"
    junit.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
        "--no-tests=error",
        "--output-junit",
        str(junit),
    ]
    log("+ " + " ".join(command))
    completed = subprocess.run(command, cwd=project, env=test_environment(build_dir, prefixes))
    if completed.returncode != 0:
        failed = build_dir / "Testing" / "Temporary" / "LastTestsFailed.log"
        if failed.is_file():
            log(failed.read_text(encoding="utf-8", errors="replace"))
        raise SystemExit(completed.returncode)
    log("ctest passed")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error
