# C++ 논리 회로 합성

`ii::builder::synthesize_hls`는 Clang의 C++23 의미 분석 결과를 타입이 있는 데이터 흐름으로 낮춘 뒤 합성 가능한 RTL을 생성한다. SDK와 빌드 호스트는 C++23을 사용한다. 회로에는 OS·SDK 런타임·인터프리터를 넣지 않는다.

자체 경로는 1~64비트 정수·bool, 정수 변환, 산술·비트·비교 연산, 분기, 정적으로 종료되는 for 루프와 순수 보조 함수를 대상으로 한다. 고정 배열은 값이 정해진 범위에서 사용한다. 루프·노드·호출 깊이에 한도를 두며 동적 할당·포인터·재귀·OS 입출력·미지원 구문을 진단한다. C++23 전체가 임의의 FPGA 회로로 변환된다는 의미는 아니다.

입출력은 ready/valid 또는 조합 논리 인터페이스를 사용한다. ready/valid 구성은 출력 레지스터 한 단계와 역압을 제공한다. 이는 임의의 연산 지연을 여러 단계로 자동 분할하는 최적화와 구분한다. 실제 클록 충족과 자원 사용은 이어지는 FPGA 구현 도구로 검증한다.

Clang 전처리·AST와 RTL·FPGA 구현을 분리하여 각 경계를 교체할 수 있도록 한다. [Clang AST](https://clang.llvm.org/docs/IntroductionToTheClangAST.html)와 [AMD HLS 언어 계약](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Coding-C/C-Functions), [AMD HLS 명령 계약](https://docs.amd.com/r/en-US/ug1399-vitis-hls/Project-Commands)을 참고한다.

## 입력 계약

소스의 top 함수는 정수 또는 bool을 반환한다. 입력은 정수·bool과 `const uint16_t (&items)[4]` 같은 고정 크기 읽기 전용 배열 참조이다. 지역 배열은 초기화 목록과 상수 인덱스 쓰기를 사용한다. 일반 포인터나 길이가 사라진 배열 매개변수는 허용하지 않는다. 지역 변수는 초기화해야 하며 mutable 전역·static 지역 상태는 거절한다.

자료형은 Clang의 고정 LP64 데이터 모델로 해석한다. `stdint.h`의 고정 폭 정수 사용을 권장한다. 정수 승격, 부호 확장, 잘라내기와 bool 변환을 데이터 흐름에 보존한다. signed 덧셈·뺄셈·곱셈은 `-fwrapv`와 같은 래핑 계약을 사용하며, C++ 기준 프로그램도 동일한 옵션으로 검증한다. signed 오른쪽 시프트는 산술 시프트이다. 시프트 수는 상수이며 피연산자 폭보다 작아야 한다. 나눗셈은 0이 아닌 상수 분모를 사용한다. 배열 범위 초과 등 C++에서 정의되지 않은 입력은 유효 입력 계약에 포함되지 않는다.

실행 가능한 순수 보조 함수는 회로 안으로 인라인하며 재귀와 16단계를 초과하는 호출은 거절한다. `for`의 조건은 합성 시점에 계산할 수 있어야 한다. 기본 전체 펼침 한도는 256회, 데이터 흐름 노드는 16,384개이다. 노드에는 공통 부분식 재사용과 상수 계산을 적용한다. 포인터·부동소수점·동적 할당·OS 호출·예외·가상 호출·범위가 정해지지 않은 루프 및 지원하지 않는 AST 구문은 진단한다. 자체 경로에서는 이해하지 못하는 HLS pragma도 거절한다.

## CLI와 공개 API

```sh
iisdk-builder hls --source examples/HLSHello/crc8.cpp --entry crc8 \
  --module crc_kernel --output build/hls --compiler clang++
```

`--interface ready-valid|combinational`, `--clock-period-ps`, `--max-unroll`, `--max-nodes`, 반복 가능한 `--include`, `--define`을 사용할 수 있다. 공개 API는 `HlsRequest`, `HlsReport`, `synthesize_hls()`이다. 하나의 작업은 독립된 출력 디렉터리를 소유해야 한다. 타임아웃과 취소는 Clang 프로세스 및 내부 분석에 적용한다.

산출물은 모듈 `.sv`, `hls.json`, `dataflow.json`, `frontend.ast.json`, `frontend.log`이다. 소프트웨어 실행 파일이나 VM 바이트코드를 회로 산출물로 대체하지 않는다. `hls.json`에는 입력 폭·배열 길이·반환 폭·펼침 수·노드 수와 인터페이스 지연을 기록한다. `requestedClockPeriodPs`는 요청 조건이며 실제 달성 주파수는 구현 도구의 타이밍 보고서에서 확인한다.

## FPGA 빌드 연결

기존 매니페스트의 kit에 다음 설정을 사용한다.

```json
{
  "id": "crc-ice40",
  "platform": "fpga",
  "target": {
    "profile": "fpga-hls",
    "part": "hx1k",
    "family": "tq144",
    "top": "hls_board",
    "sources": ["board.v"],
    "constraints": ["ice40.pcf"],
    "hls": {
      "source": "crc8.cpp",
      "entry": "crc8",
      "module": "crc_kernel",
      "compiler": "clang++",
      "implementation": "ice40",
      "clockPeriodPs": 83333
    }
  }
}
```

`implementation`은 `rtl`, `ice40`, `vivado`, `quartus`이다. RTL 구성은 Yosys로 계층·프로세스·연결을 검사하고 netlist도 생성한다. 이 산출물은 `synthesized-rtl`이며 장치 프로그래밍 요청을 거절한다. 물리 구성은 생성된 커널과 선택적 RTL 보드 래퍼를 함께 구현하고 기존 비트스트림 검증을 거친다. iCE40에는 실제 요청 주파수를 전달하며 Vivado·Quartus에는 클록 제약을 생성한다. 래퍼의 클록 이름은 `clockPort`로 선택하며 기본값은 `clk`이다.

HLS는 `synthesize` 단계로 기록한다. 여기서 실패하면 RTL 구현·검증·프로그래밍으로 넘어가지 않는다. 입력·타입 분석, 회로 생성, 공급자 FPGA 구현은 별도의 변경 경계이다. Vitis·SmartHLS 등 기존 외부 상용 프로파일은 각 공급자의 언어·인터페이스·스케줄링 계약을 사용하며 자체 C++23 합성의 지원 범위와 구분한다.

## 검증

`tests/hls`는 공개 API, 7개 정수 커널, 미지원 구문·분석 예산·취소, 매니페스트 왕복, 합성 실패 전파를 검사한다. Yosys·Verilator가 설정되면 `iiSDKBuilder.hls_equivalence`가 실제 C++ 원본과 생성 RTL을 대조한다. CRC-8의 65,536개 입력 조합 및 추가 난수 입력을 포함한 75,536개 거래에 대해 7개 커널을 비교하고, ready/valid 대기·데이터 유지·리셋을 확인한다. 이 검사는 `hls-execution` 라벨을 가지며 GUI를 열지 않는다.

빌드 테스트에는 Clang이 필요하다. 전체 회로 대조 검사는 `IISDK_HLS_YOSYS`, `IISDK_HLS_VERILATOR` 경로를 설정하여 활성화한다. 물리 장치 업로드와 상용 Vivado·Quartus 실기 검증은 별도의 공급자 도구·장치가 필요하며 합성·시뮬레이션 결과와 혼동하지 않는다.

C++17 이후의 복합 대입은 우변을 먼저 평가한 뒤 좌변을 읽는다. `compound_constant`의 `x += ++x` 회귀 검사는 이 순서를 상수 데이터 흐름에서 확인하며, 동일한 구문을 포함한 커널을 실제 RTL과 C++ 기준 프로그램으로도 대조한다. 부분 초기화 배열의 명시 값과 나머지 원소의 0 채우기도 대조 대상이다.
