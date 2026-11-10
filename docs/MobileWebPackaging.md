# 모바일·웹 패키징 명세

Android는 앱의 NativeActivity 공유 라이브러리와 `DT_NEEDED` 연쇄를 대상 NDK의
ELF 도구로 검사한다. 앱이 연결한 라이브러리만 ABI별 `lib/<abi>/`에 담으며,
NDK 시스템 스텁은 복사하지 않고 `libc++_shared.so` 같은 실제 런타임은 포함한다.
AndroidManifest.xml과 필요한 SDK 플랫폼·빌드 도구가 없거나 누락·충돌하는
라이브러리가 있으면 실패한다. 공유 라이브러리는 압축하지 않고 16 KiB 경계에 정렬하여 추출 없는 로딩을
지원한다. 서명 정보가 없는 APK는 미서명으로 표시하며
배포용 서명을 주장하지 않는다. Manifest, 앱 자산과 사전 컴파일된 DEX는 앱이
소유한다. 패키지 구조 검사는 실제 기기에서 앱을 실행하지 않는다.

앱의 `resources/android/AndroidManifest.xml`, `App/AndroidManifest.xml` 또는
루트의 `AndroidManifest.xml`을 자동으로 찾는다. 다른 경로는 앱 또는 키트의
cache에서 `IISDKBUILDER_ANDROID_MANIFEST`로 지정한다. Android 도구 경로는
`IISDKBUILDER_ANDROID_SDK`, `IISDKBUILDER_ANDROID_NDK` 또는 Android SDK 환경과
NDK 툴체인에서 찾으며, `IISDKBUILDER_ANDROID_ASSETS`,
`IISDKBUILDER_ANDROID_RES`, `IISDKBUILDER_ANDROID_DEX`는 선택 사항이다.
컴파일된 manifest가 코드를 필요로 하면 유효한 `classes.dex`를 제공해야 한다.

iOS는 일반 dylib를 앱에 임의로 넣지 않고 `.framework` 번들의 연결과 내장을
사용한다. 프레임워크의 플랫폼과 앱 아키텍처를 검증하며 기기 배포에는 개발자
서명과 프로비저닝이 필요하다. WebAssembly는
Emscripten 메인 모듈과 사이드 모듈 및 로더 자산의 실제 관계를 확인한다.
Emscripten은 링크 명령에 명시된 사이드 모듈을 심볼 사용 여부와 무관하게
메인 모듈의 로드 목록에 기록할 수 있으므로, 웹 앱의 CMake 링크 목록에는
실제로 필요한 모듈만 명시해야 한다. 빌더는 이 목록을 임의로 고쳐 쓰지 않는다.
링커에 드러나지 않는 런타임 플러그인은 앱 또는 키트 cache의
`IISDKBUILDER_RUNTIME_MODULES`에 파일명 목록으로 명시한다. Android는 `lib*.so`,
iOS는 `Name.framework`, 웹은 로더가 기대하는 상대 모듈 이름을 사용한다.
각 대상에서 지원되지 않는 산출물이나 해석되지 않은 의존성을 성공으로
보고해서는 안 된다. 패키지 보고서의 `packageSigning`은 모바일에서
`unsigned`이며 실행·기기 설치가 완료되었다는 뜻이 아니다.

통합 테스트는 기존 180초 제한을 유지한다. POSIX에서 제한을 넘기면 빌더에
종료 신호를 보내 소유한 컴파일 작업을 취소하도록 요청하고 최대 20초 동안
종료를 기다린다. 그 뒤에도 끝나지 않는 프로세스는 강제 종료한다. 제한을
초과한 테스트는 취소 정리에 성공하더라도 실패로 기록한다.
