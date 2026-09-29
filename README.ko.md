# R Spectrum for Haiku OS

[English](README.md)

실시간 오디오 스펙트럼 분석기입니다. 시스템의 오디오 입력을 받아 주파수
스펙트럼을 로그 축 위의 막대로 그립니다.

분석 부분은 이 저장소의 Linux·macOS 빌드와 공유하며, 플랫폼마다 다른 것은
오디오 입력을 받는 방법과 화면에 그리는 방법뿐입니다.

## 요구 사항

Haiku(x86 또는 x86_64). Haiku 기본 키트 외에 필요한 것은 없습니다.

## pkgman으로 설치

| Haiku | 명령 |
| --- | --- |
| 32비트 x86(x86_gcc2) | `pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2`<br>`pkgman install rspectrum` |
| x86_64 | `pkgman add-repo https://pkgman.rainygirl.com/x86_64`<br>`pkgman install rspectrum` |

설치한 뒤 Deskbar -> Applications 에서 **R Spectrum** 을 실행하십시오.

`pkgman add-repo` 가 `Operation not supported` 로 실패하면 그 이미지의
네트워크 키트에 TLS 가 없는 것입니다. 주소를 `https://` 대신 `http://` 로
바꿔 주십시오.

## 소스에서 설치

Haiku 기기에서 직접 실행하십시오.

```sh
cd haiku
./install.sh
```

컴파일한 뒤 실행 파일을 `~/config/non-packaged/apps/` 에 넣고,
**Deskbar -> Applications** 에 등록하고, 바탕화면에 링크를 만듭니다.

```sh
./install.sh --build-only   # 빌드만 하고 설치하지 않습니다
./install.sh --uninstall    # 다시 제거합니다
```

## 다른 플랫폼

`linux/` 와 `macos/` 에 각 시스템용 포트가 Makefile 과 함께 들어 있습니다.

## 라이선스

MIT

## AI 사용 고지

이 프로그램은 Claude 와 함께 작성했습니다.
