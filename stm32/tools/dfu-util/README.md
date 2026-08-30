# dfu-util (Windows only)

`win/dfu-util.exe` 는 dfu-util 프로젝트가 배포하는 **0.11 공식 윈도우 바이너리**
(`win64/dfu-util-static.exe`)를 이름만 바꾼 것입니다. 정적 링크판이라 libusb DLL 이
함께 다니지 않습니다.

- 출처: https://dfu-util.sourceforge.net/releases/dfu-util-0.11-binaries.tar.xz
- 소스: https://dfu-util.sourceforge.net/releases/dfu-util-0.11.tar.gz
- 프로젝트: https://dfu-util.sourceforge.net/

## 왜 따로 넣었나

부트로더를 STM32 ROM 부트로더로 구우려면 `-s`(`--dfuse-address`) 로 주소를 지정해야
합니다. STM32Tools 가 dfu-util 을 함께 배포하지만 플랫폼마다 판본이 다릅니다.
실제 바이너리에서 읽은 값입니다.

| 플랫폼 | STM32Tools 2.4.0 판본 | `-s` |
|---|---|---|
| macOS | 0.11 (유니버설 x86_64+arm64) | 있음 |
| Linux x86_64 | 0.8 | 있음 |
| Linux aarch64 | 0.11-dev | 있음 |
| **Windows** | **0.1+svn, (C) 2007-2008 OpenMoko** | **없음** |

윈도우 판본은 DfuSe 자체보다 오래된 것이라 `invalid option -- s` 로 종료 코드 2 를
내고 끝납니다. macOS 는 오히려 STM32Tools 쪽이 낫습니다 — 공식 배포본의 darwin 은
x86_64 전용이라 Apple Silicon 에서 Rosetta 가 필요합니다.

그래서 **윈도우만** 이 바이너리를 쓰고 나머지는 STM32Tools 를 그대로 씁니다.
어느 것을 쓸지는 `dfu-util.sh` 가 정합니다.

ST 가 윈도우 판본을 갱신하면 `win/` 과 `dfu-util.sh` 를 지우고
STM32Tools 의 `dfu-util.sh` 를 바로 부르면 됩니다.

## 라이선스

dfu-util 은 **GPLv2** 입니다. 전문은 `COPYING` 에 있고, 대응 소스는 위 링크에서
받을 수 있습니다.

이 라이선스는 별개 프로그램으로 실행되는 dfu-util 자체에만 적용됩니다. 이 패키지의
코어, variant, 라이브러리와 이것으로 빌드한 스케치에는 영향이 없습니다.
