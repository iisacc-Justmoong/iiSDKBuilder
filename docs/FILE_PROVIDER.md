# File creation ownership in iiSDKBuilder

소스는 `src/Build/`, `src/Core/`, `src/Resources/`, `src/Targets/`,
`src/HLS/`로 책임에 따라 구분한다. 공개 헤더와 대응 구현은 함께 두며,
빌드와 설치 소비자의 `<iiSDKBuilder/...>` 경로는 유지한다.

CMake query files, project identity/report JSON and platform process logs use native provider streams and provider atomic publication. Build orchestration and platform-kit rules stay in Builder.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/Build/Builder.cpp`
- `src/Core/Json.cpp`
- `src/Build/Process.cpp`
# Core foundation migration (0.13)

Builder's JSON/manifests/reports use `iiFileProvider::native::File::write` directly.
Atomic file implementation no longer comes from iiSDKCore. The existing Native
dependency supplies atomic publication and file presentation; Core remains a
lower runtime dependency. `iiSDKBuilder.json` verifies replacement and failure
preservation in addition to JSON parsing.

## Shared process runner (0.1.1)

Core 0.15 owns the child process and nonblocking standard pipes. Builder writes
the merged output sink through Native OutputFile, including close/presentation
notification, and flushes before reporting success. No raw file handles are
passed to Core and no log file creation is moved into the foundation module.
