# Congregation 네이티브 키트 조립

기본 스테이징 위치는 SDK/build/congregation-native/prefix이다. 사용자 설치본과 섞지 않고 C++23과 같은 최소 OS로 빌드한다. 외부 정적 코덱·TLS 키트는 `tools/build_native_kit.py`의 버전·SHA-256 매니페스트로 조립한다. Sodium과 Vulkan 로더도 대상 플랫폼에 맞는 빌드가 필요하다.

설치 순서는 iiSDKCore → iiJson → iiFileProvider Native/CoreIO/Compatibility → iiPaintEngine → iiImageCodec → iiNetwork → iiAcountManager Native → iiLicenseManager Native → iiUpdateManager Native → iiSharedCanvas Native → LVUI → iiGUI → LVRS Native이다. 각 수정 모듈의 `install-native.sh`는 저장소의 build/에서 구성·빌드·검사·설치를 수행한다. `IISDK_NATIVE_PREFIX`로 같은 설치 위치를 지정한다. iiNetwork는 기존 `install.sh`를 유지하며 `INSTALL_PREFIX`와 `CMAKE_PREFIX_PATH`로 같은 키트를 선택한다. 운영체제의 libcurl·libxml2, 정적 OpenSSL과 이미지 코덱을 선택하는 CMake 인수는 해당 설치 환경에 맞춘다.

공개 헤더나 값 타입을 바꾸면 그 헤더를 소비하는 정적 라이브러리도 다시 빌드해야 한다. 기존 LVRS와 새 iiGUI의 WidgetSnapshot ABI를 혼합하면 구성 성공 뒤에도 스택 손상이 발생할 수 있다. 모든 소비 모듈을 같은 키트로 다시 빌드한 뒤 `tools/freeze_native_kit.py --prefix <prefix>`로 헤더·패키지·라이브러리·도구·라이선스 해시를 고정한다. `--check`는 누락·추가·변경 파일을 감지한다. Qt 공개 헤더와 Qt 패키지 탐색도 거부한다.

이 매니페스트는 설치된 파일의 일관성 증거이다. 최종 앱의 동적 의존성, 서명, 최소 OS 선언과 재배치 실행은 제품의 별도 검사로 확인한다. macOS에서 Windows·Linux 소스가 존재한다는 사실을 해당 운영체제 실행 결과로 기록하지 않는다.

빌더의 JSON 숫자도 iiSDKCore의 유한 십진 변환을 사용한다. macOS 12에서 제공하지 않는 부동소수점 charconv 오버로드를 링크하지 않으며, 부호 있는 0·비정규 수·최대 유한 수의 왕복을 검사한다.
