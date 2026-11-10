# CPython 스크립트 AOT 빌드

iiSDKBuilder의 CMake 함수 `iisdk_builder_cpython_aot`는 지정한 CPython
스크립트마다 네이티브 확장 모듈을 빌드한다. 생성 모듈을 iiSDKCore의 선택적
`iiSDKCore::CPython` 호스트가 애플리케이션 소유 `SerialQueue`에 부착하여 호출한다.
Core가 Builder나 iiXml에 역의존하지 않으며 앱이 구성요소를 조합한다.

첫 백엔드는 Apache 2.0 라이선스의 Nuitka 2.8.9와 CPython 3.9–3.13을 사용한다.
Nuitka는 Builder에 포함되지 않으며, 해당 버전 소스 배포판의 `bin/nuitka` Python
스크립트를 준비해 `NUITKA_LAUNCHER`로 지정한다. 선택한 CPython의 헤더와
네이티브 C 컴파일러도 빌드 환경에 필요하다.
CPython 구현체·버전·컴파일러 버전을 확인하고 일치하지 않으면 빌드를 실패시킨다.
`iiSDKCore::CPython`을 먼저 구성한 경우 Core와 AOT 인터프리터의 확장 모듈 ABI도
CMake 구성 단계에서 비교한다.
생성 코드는 대상 CPython 라이브러리와 일치해야 한다. Nuitka 확장 모듈 컴파일은
호스트 네이티브 빌드이며 별도의 교차 컴파일 성공을 암시하지 않는다. 스크립트의
동적 import·eval·exec와 외부 모듈은 동일 CPython 런타임에서 동작한다. 입력으로
지정한 모듈만 AOT 대상으로 보장하며 동적 의존성은 앱이 제공한다.

함수의 `SOURCES`는 원본 `.py` 경로이며 `OUTPUT_DIRECTORY`는 빌드 산출물
디렉터리다. `PYTHON_EXECUTABLE`과 `NUITKA_LAUNCHER`는 같은 호스트 CPython에
연결된 도구를 가리킨다. 동일한 모듈 이름의 중복 입력은 오류다. AOT 결과는
CPython 확장 모듈이므로 파일 이름을 바꾸지 않는다. 출력 경로와 모듈 의존성은
생성 소비자 대상의 CMake 의존성에 연결된다.
현재 입력은 유효한 최상위 모듈 이름을 가진 개별 `.py` 파일이다. 패키지 계층을
보존하는 AOT 구성과 스크립트의 `__main__` 진입점 실행은 제공하지 않는다.
Core에서는 컴파일된 모듈의 함수를 호출하며 패키지 의존성은 앱이 제공한다.
`RUNTIME_TARGETS`로 전달한 공유 라이브러리는 컴파일된 모듈 옆에 복사된다.

```cmake
find_package(iiSDKBuilder CONFIG REQUIRED)
find_package(iiSDKCoreCPython CONFIG REQUIRED)
find_package(iiXmlNative CONFIG REQUIRED)

iisdk_builder_cpython_aot(TARGET app_cpython_modules
    PYTHON_EXECUTABLE "${APP_CPYTHON_EXECUTABLE}"
    NUITKA_LAUNCHER "${APP_NUITKA_LAUNCHER}"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cpython-modules"
    SOURCES "${iiXmlNative_MATRIX_PYTHON_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app_logic.py"
    RUNTIME_TARGETS iiXml::MatrixC)
add_dependencies(app app_cpython_modules)
target_link_libraries(app PRIVATE iiSDKCore::CPython)
```

앱은 `CPythonHost::attach`에 위 출력 디렉터리를 전달하고 `queries()`에
모듈명·함수명·`core::Value` 인자를 전송한다. 모듈 탐색과 Python 객체 호출은
Core 직렬 큐에서 수행된다. 내장 CPython 라이브러리와 표준 라이브러리는 대상
앱의 배포 구성에서 제공해야 하며 Builder 실행 파일은 런타임에 필요하지 않다.

iiXml의 `iixml_matrix.py`도 이 함수의 입력으로 컴파일하며, 앱이 iiXml의 C
라이브러리를 제공하고 Core 호스트의 검색 경로에 AOT 출력 디렉터리를 전달한다.
별도 플랫폼과 장치 실행은 각각 검증되어야 한다.
통합 테스트는 컴파일된 iiXml에서 배치 행렬 곱, 축소, 텐서 곱과 XML 왕복,
비수치 입력 오류 및 동적 Python 실행을 함께 확인한다.
