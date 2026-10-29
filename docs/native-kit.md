# 데스크톱 네이티브 의존성 키트

`tools/build_native_kit.py`는 OpenSSL, libjpeg-turbo, libpng, libwebp와 libtiff의 버전·공식 소스·SHA-256을 고정하여 정적 라이브러리로 설치한다. SDK 실행 코드는 패키지 관리자 경로를 요구하지 않는다. 다운로드·원본·빌드·로그는 `build/native-kit/`, 설치는 명시한 `build/` 하위 prefix에 둔다. 기존 시스템 설치본은 변경하지 않는다.

```sh
python3 tools/build_native_kit.py --prefix ../build/congregation-native/prefix --minimum-macos 12.0
```

macOS와 Linux의 호스트 빌드를 제공한다. Windows OpenSSL은 MSVC 전용 레시피가 필요하며 이 도구가 Windows 실행을 입증하지는 않는다. macOS에서는 CMake와 OpenSSL 모두 같은 최소 OS와 아키텍처로 빌드한다. SSL은 지원 기간이 긴 3.5 LTS 계열을 사용한다. 체크섬 불일치 입력은 압축을 풀거나 빌드하지 않으며, 압축 해제는 Python의 data 필터를 적용한다.

TIFF 키트는 PNG/JPEG/WebP와 시스템 zlib를 함께 제공한다. LERC·JBIG·LZMA·Zstd 외부 압축 모듈은 포함하지 않으며 지원하지 않는 파일은 코덱 오류로 반환한다. ICC·방향·해상도와 RGBA 정밀도는 iiVisualCodec에서 검사한다. 소스 버전/체크섬/빌드 결과와 재배포 라이선스는 키트에 남긴다.

키트 빌드와 별도로 각 소비 SDK의 설치 소비자 검사, 최종 앱의 동적 의존성·최소 OS·재배치 실행을 수행해야 한다.
