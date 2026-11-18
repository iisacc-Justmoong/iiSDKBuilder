# GitHub 호스티드 빌드 검사

이 저장소의 푸시는 GitHub 호스티드 러너 `ubuntu-latest`와 `windows-latest`에서 각각 한 번씩 구성·빌드·테스트를 실행한다. 같은 파일 `.github/workflows/build-test.yml`은 `workflow_call`로 다른 iiSDK C++ 저장소가 호출하는 재사용 워크플로이다. 호출 쪽은 OS 매트릭스를 다시 두지 않는다. self-hosted 러너는 사용하지 않는다.

워크플로 동시성 그룹은 호출한 저장소와 ref이다. 같은 ref에 새 푸시가 오면 진행 중인 실행을 취소한다. 매트릭스는 `fail-fast: false`이다. 한 OS의 실패가 다른 OS 실행을 중단하지 않는다.

## 호출 예

iiSDKCore는 iiSDK 의존성이 없는 tier 0이다. 호출 워크플로는 다음으로 충분하다.

```yaml
name: Build test
on:
  push:
jobs:
  build-test:
    uses: iisacc-Justmoong/iiSDKBuilder/.github/workflows/build-test.yml@main
    with:
      build_type: Release
      build_tests: true
```

의존 모듈이 있으면 `dependencies`에 설치 순서대로 한 줄에 하나씩 적는다. `@ref`를 생략하면 그 저장소의 기본 브랜치를 받는다. `--` 뒤는 그 의존성만의 CMake 구성 인수이다.

```yaml
name: Build test
on:
  push:
jobs:
  build-test:
    uses: iisacc-Justmoong/iiSDKBuilder/.github/workflows/build-test.yml@main
    with:
      build_type: Release
      build_tests: true
      apt_packages: pkg-config
      choco_packages: nuget
      cmake_args: -DIISDKBUILDER_CORE_SOURCE_DIR=@dep:iiSDKCore@
      dependencies: |
        iisacc-Justmoong/iiSDKCore@master -- -DBUILD_TESTING=OFF -DIISDKCORE_BUILD_EXAMPLES=OFF
        iisacc-Justmoong/iiFileProvider@master -- -DBUILD_TESTING=OFF -DIIFILEPROVIDER_BUILD_QT_API=OFF -DIIFILEPROVIDER_USE_BUNDLED_SQLITE=ON
```

`@dep:모듈이름@`은 그 의존성 소스 체크아웃의 절대 경로로 바뀐다. 한 줄에 여러 의존성을 적을 때는 항목 사이를 ` || `로 구분할 수도 있다.

## 입력

| 입력 | 기본값 | 의미 |
| --- | --- | --- |
| `build_type` | `Release` | `CMAKE_BUILD_TYPE` |
| `build_tests` | `true` | `BUILD_TESTING`을 켜고 `ctest --output-on-failure --no-tests=error`를 실행한다. `false`이면 구성과 빌드만 한다. |
| `cmake_args` | 비어 있음 | 대상 프로젝트의 CMake 구성에 덧붙이는 인수 |
| `apt_packages` | 비어 있음 | Ubuntu에서 추가로 설치할 apt 패키지. 공백으로 구분한다. |
| `choco_packages` | 비어 있음 | Windows에서 추가로 설치할 Chocolatey 패키지. 공백으로 구분한다. |
| `dependencies` | 비어 있음 | 대상보다 먼저 받아 빌드하고 설치할 iiSDK 저장소 |

Linux 기본 컴파일러는 시스템의 GCC이다. Ninja와 CMake 3.31 이상을 함께 설치한다. Ubuntu Clang 18은 `__cpp_concepts`를 201907로 보고, libstdc++의 `<expected>`는 202002 이상을 요구하므로 그 Clang으로는 `std::expected`를 쓰는 iiSDKCore가 컴파일되지 않는다. Clang과 `clang-scan-deps`는 항상 설치한다. 호출자가 `-DCMAKE_CXX_COMPILER=clang++`를 넘기고 표준 라이브러리가 그 Clang을 받아들이면 그 컴파일러가 우선하며, 이때 CMake 모듈 스캔은 `clang-scan-deps`를 사용한다. Windows는 Visual Studio의 `vcvars64` 개발자 환경에서 MSVC와 Ninja를 사용한다. CMake 모듈 스캔은 MSVC의 의존성 스캐너를 사용하며 `clang-scan-deps`를 요구하지 않는다.

컴파일러 캐시는 Linux에서 ccache, Windows에서 sccache이다. 캐시 디렉터리는 러너 워크스페이스의 `.ccache`와 `.sccache`이다.

테스트가 실패하면 `project/build/Testing`의 CTest 로그를 아티팩트 `test-logs-<os>`로 올린다.

## 의존성이 찾아지는 방식

의존성은 `$HOME/.local/SDK/<모듈>`에 설치된다. Windows에서 `HOME`이 비어 있으면 `USERPROFILE`을 `HOME`으로 둔다. 이미 설치한 접두어는 다음 구성의 `CMAKE_PREFIX_PATH`에 순서대로 들어간다. 소스는 대상 프로젝트의 형제 디렉터리 `../<모듈>`에 받는다.

iiSDKCore, iiJson, iiGUI, iiNetwork, iiFileProvider와 이 저장소를 기준으로 확인한 내용은 다음과 같다.

- 설치 접두어 `~/.local/SDK/<모듈>`은 iiGUI, iiFileProvider, iiNetwork, 이 저장소의 설치 명령과 같다.
- `find_package`의 `HINTS "$ENV{HOME}/.local/SDK/<모듈>"`은 iiNetwork의 iiSDKCore·iiJson 검색과 이 저장소의 iiFileProvider 검색에서 사용된다.
- iiGUI의 `find_package(iiSDKCore)`에는 HINTS가 없다. `CMAKE_PREFIX_PATH`가 없으면 설치본을 찾지 못한다.
- Windows 문서의 일부 모듈은 자신의 설치 접두어만 `USERPROFILE`로 되돌린다. `$ENV{HOME}`을 읽는 HINTS는 `HOME`이 비어 있으면 그 경로를 보지 않는다. 워크플로가 `HOME`을 채우는 이유이다.
- 이 저장소의 테스트는 `IISDKBUILDER_CORE_SOURCE_DIR`이 없을 때 `../iiSDKCore` 소스를 찾는다. 형제 체크아웃이 그 경로이다.
- 샘플 CMakeLists에서는 `../../SDK/<모듈>` HINTS를 찾지 못했다. 워크플로는 그 형태를 쓰는 소비자를 위해 설치 접두어를 대상 소스 기준 `../../SDK/<모듈>`에도 연결한다.

의존성 줄에 `-DBUILD_TESTING`이 없으면 의존성 테스트는 끈다. 대상 저장소의 테스트만 한 번 실행한다.

## Qt 6.8.3과 Apple 전용 대상

이 버전의 워크플로는 Qt 6.8.3과 Apple 전용 프레임워크를 설치하지 않는다. 샘플에서 `/Volumes/Storage/Qt/6.8.3`은 `find_package` 경로가 아니라 macOS 작업 트리 기록에 남아 있다. Windows 문서의 `C:/Qt/6.8.3/msvc2022_64`도 이 러너에는 없다. `Qt6_DIR`을 그 경로로 넘기지 않는다.

Qt를 요구하는 대상은 그 저장소의 옵션으로 끈다. iiFileProvider는 `-DIIFILEPROVIDER_BUILD_QT_API=OFF`이다. 시스템 SQLite가 없는 러너에서는 `-DIIFILEPROVIDER_USE_BUNDLED_SQLITE=ON`으로 저장소에 고정된 SQLite 소스를 컴파일한다.

AppKit, Metal, UIKit, Foundation, ImageIO, QuickLookThumbnailing 링크는 iiGUI와 iiFileProvider Native에서 `APPLE` 또는 iOS 조건 안에 있다. 이 두 러너의 호스트 구성은 그 소스를 컴파일하지 않는다. Apple 툴체인 파일을 넘기거나 iOS 키트를 켜지 않는다.

## 이 저장소의 푸시

입력을 비운 채 이 저장소를 구성하면 워크플로가 다음을 적용한다.

- iiSDKCore `master`를 테스트 없이 설치하고, 소스를 `-DIISDKBUILDER_CORE_SOURCE_DIR=@dep:iiSDKCore@`로 넘긴다.
- iiFileProvider `master`는 Qt API와 도구와 테스트를 끄고, 번들 SQLite로 Native를 설치한다.
- Ubuntu에는 `clang`과 `clang-tools`를, Windows에는 `llvm`을 더한다. HLS 구성이 `clang++`을 찾기 때문이다.

호출자가 `dependencies` 또는 `cmake_args`를 직접 넘기면 그 값이 위 기본값을 대체한다. apt와 Chocolatey 추가 패키지는 iiSDKBuilder를 빌드할 때 위 컴파일러 패키지에 더해진다.
