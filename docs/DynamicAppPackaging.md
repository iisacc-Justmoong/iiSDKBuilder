# 앱의 동적 SDK 패키징

기본 데스크톱 앱 빌드는 SDK에 `BUILD_SHARED_LIBS=ON`을 요청하고,
실제 실행 파일에 연결된 공유 라이브러리와 그 연쇄 의존성만 앱 전용 패키지에
넣는다. macOS·iOS의 링커에는 `-dead_strip_dylibs`, Linux·Android 링커에는
`--as-needed`를 기본 적용해 링크 목록에 있지만 심볼을 사용하지 않은 공유
라이브러리도 제외한다. 생성자 실행 같은 부작용만을 위해 링크해야 하는 경우에는
앱·SDK 또는 키트 cache에 `IISDKBUILDER_PRUNE_UNUSED_SHARED=OFF`를 명시한다.
Windows에서는 연결 도구의 기본 참조 제거 동작을 사용하며 이 동작을 아직 실제
Windows에서 검증하지 않았다. SDK 전체 설치 디렉터리를 복사하지 않는다.
운영체제 제공 라이브러리는 포함하지 않는다. 동일 파일명 충돌, 해석되지 않은 의존성 또는 필요한 도구의
부재는 오류로 보고한다. 기존 패키지는 새 패키지가 완성될 때까지 보존하며,
중단된 이전 패키징 작업이 남았다면 이를 임의로 지우지 않고 오류로 알린다.
결과와 패키지 상태는 빌드 보고서에 기록한다.

macOS의 앱 번들은 `Contents/Frameworks`를 필요한 런타임 라이브러리로
다시 구성하고, 그 밖의 앱 번들 리소스는 보존한다. 일반 실행 파일은 패키지의 `lib`,
Windows 실행 파일은 실행 파일 옆에 의존성을 둔다. Linux는 `lib`을 사용한다.
macOS는 패키지 안의 경로를 가리키도록 Mach-O 연결 정보를 바꾸고 재서명한다.
Linux의 경로 수정에는 `patchelf`가 필요하다. 런타임에 이름으로 불러오는
플러그인은 링크 정보만으로 추론할 수 없다. 앱 manifest의 `cache`에
`IISDKBUILDER_RUNTIME_MODULES`를 지정하면 선택된 SDK prefix의 `lib`·`bin`과
기존 macOS 앱의 `Contents/Frameworks`에서 세미콜론으로 구분된 정확한
파일명만 찾아 함께 패키징한다. 키트별
cache로 플랫폼에 맞는 이름을 덮어쓸 수 있다. 중복되거나 누락된 이름은
오류이며 앱은 자신의 패키지 내부 경로로 플러그인을 열어야 한다.

CMake에서 명시적으로 `STATIC`으로 선언된 SDK는 이 요청만으로 공유 라이브러리가
되지 않는다. 보고서의 `linkage`는 기본값이 공유 연결을 **요청**했다는 뜻이며,
macOS·Linux의 링크 명령에서 SDK prefix 내부 `.a`가 발견되면
`contains_static_sdk`와 `staticSdkLinks`로 알리고 기본 패키징을 실패시킨다.
정적 AOT를 선택하거나 앱 또는 키트 cache에 `IISDKBUILDER_ALLOW_STATIC_MODULES=ON`을
명시한 경우에는 정적 링크를 허용한다. 이 검사는 이름 기반이며
모든 우회 연결을 증명하지 않는다. Windows의 `.lib`는 확장자가 아니라 COFF
아카이브 멤버를 검사해 정적 코드, DLL 가져오기 또는 혼합을 구분하며,
판별하지 못하는 입력은 추측하지 않고 실패한다. 함수 단위로 공유 라이브러리
내용을 축소한다는 의미도 아니다.

명시적으로 `IISDKBUILDER_STATIC_NATIVE_AOT=ON`을 앱의 cache에 지정하면 기존
정적 Core 카탈로그와 Python 부분집합 AOT 경로를 선택할 수 있다. Android·iOS·
WebAssembly의 패키징 계약은 [모바일·웹 패키징](MobileWebPackaging.md)에
설명하며 모바일 결과는 미서명이다. 임베디드 타깃은 자동 앱 패키징을 지원하지
않는다. 컴파일 산출물만 필요한 앱 또는 키트는 cache에
`IISDKBUILDER_PACKAGE=OFF`를 명시할 수 있으며 보고서에는 `not_requested`가
남는다. 하드웨어 어댑터에는 별도의 보드 산출물 계약이 적용된다. 크로스 컴파일
중 다른 운영체제 대상의 의존성은 호스트용 검사 도구로 대신 해석하지 않는다.

패키징 검사는 실행 파일을 실행하지 않는다. 배포용 인증서, 권한 및 공증은
빌드 결과의 애드혹 서명과 별도의 앱 배포 절차이다.
