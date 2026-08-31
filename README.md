# baram-stm32-arduino

BARAM 의 STM32 보드용 아두이노 보드 패키지.
[STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) 2.12.0 에서
이 보드들에 필요한 것만 추려 만들었습니다.

## 보드

| 보드 | MCU | 비고 |
|---|---|---|
| **WEACT-H750-MINI** | STM32H750VBT6 | 자체 부트로더. 스케치가 외부 QSPI 플래시에서 실행됩니다 |
| HiGenis Dummy | — | HiGenis 그룹이 메뉴에 보이게 하는 자리표시자 |

## 설치

*Preferences > Additional boards manager URLs* 에 **두 개 다** 넣습니다.

```
https://raw.githubusercontent.com/chcbaram/baram-stm32-arduino/main/package_baram_stm32_index.json
https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
```

두 번째가 반드시 필요합니다. 이 패키지는 툴체인을 직접 갖고 있지 않고
컴파일러, OpenOCD, STM32Tools, CMSIS 를 STMicroelectronics 가 배포하는 것에
의존합니다. Board Manager 가 알아서 받아옵니다.

그다음 *Tools > Board > Boards Manager* 에서 **BARAM STM32 Boards** 를 설치합니다.

```
Tools > Board             > BARAM STM32 Boards > BARAM
Tools > Board part number > WEACT-H750-MINI
```

## WEACT-H750-MINI

STM32H750 의 내부 플래시 128KB 는 지우기 단위가 하나뿐이라 부트로더만 들어갑니다.
**스케치는 외부 QSPI 플래시에 링크되고 거기서 그대로 실행됩니다**
(W25Q64, 8MB, `0x90000000` 에 메모리 맵).

```
0x90000000  4K       TAG 섹터      부트로더가 검증 후 기록
0x90001000  1K       앱 벡터       VTOR 이 여기를 가리킨다
0x90001400  1K       firm_ver_t    .version 섹션
0x90001800  8M - 6K  앱 코드
```

알아둘 것이 셋 있습니다.

- **QSPI 커널 클럭은 절대 멈추면 안 됩니다.** CPU 가 거기서 명령어를 가져오고
  있기 때문입니다. 부트로더가 QUADSPI 를 D1HCLK 에 물려 넘겨주고, D1HCLK 은
  SYSCLK 을 따라가므로 `SystemInit()` 이 SYSCLK 을 HSI 64MHz 로 내려도 멈추지는
  않습니다. 그래서 variant 는 PLL1 을 480MHz 로 올리되 `RCC_PERIPHCLK_QSPI` 는
  **선택하지 않습니다.** QSPI 를 PLL 에 물리면 `SystemInit()` 이 PLL 을 끄는
  순간 명령어 인출이 멈추고, 폴트조차 못 냅니다. SCK 는 D1HCLK/2 = 120MHz 로
  W25Q64JV 의 133MHz 한계 안입니다.
- **후처리 태깅 도구가 필요 없습니다.** variant 가 `.version` 에 `firm_ver_t` 를
  넣고, 부트로더가 거기서 크기를 읽어 CRC 를 스스로 계산해 첫 부팅에 TAG 로
  승격시킵니다.
- **부트로더를 다시 구워도 스케치는 그대로입니다.** QSPI 내용과 TAG 가 살아남아
  원래 있던 앱으로 바로 부팅합니다.

### 업로드

USB 가 기본으로 CDC 라 스케치는 항상 열거되고, 전원을 받는 그 케이블로 언제든
교체할 수 있습니다. USB 만 있는 다른 아두이노 보드와 같은 방식입니다 —
1200bps 터치를 받으면 스케치가 부트로더로 재부팅하고, `tools/baramdl` 이
부트로더의 CDC 로 새 이미지를 씁니다.

스케치가 직접 부를 수도 있습니다.

```cpp
rebootToBootloader();       // 부트로더에 머무른다
rebootToBootloader(true);   // UF2 대용량 저장 볼륨까지 띄운다
```

USB 를 끈 채 빌드했거나 USB 가 올라오기 전에 죽는 스케치라면,
**300ms 안에 리셋을 두 번 누르면** 부트로더가 상주합니다. 이 경로는 전적으로
부트로더가 처리하므로 스케치가 무엇을 하든 동작합니다.

| 방법 | 필요한 것 | |
|---|---|---|
| Bootloader USB (CDC) | USB 케이블 | 기본. 완전 자동, 약 270 KB/s |
| UF2 mass storage | 리셋 더블탭 | `.uf2` 를 드라이브에 복사 |
| OpenOCD QSPI (SWD) | PA13/PA14 에 ST-LINK | `debugger/weact_h750_qspi.cfg` |

빌드할 때마다 `.bin` 옆에 `.uf2` 도 만들어지고, *Sketch > Export Compiled Binary*
가 스케치의 `build/` 에 넣어줍니다. 리셋을 두 번 눌러 `H750BOOT` 드라이브를
띄우고 끌어다 놓으면 됩니다 — 도구가 전혀 필요 없고, 복사가 끝나면 **리셋 없이
스케치가 바로 실행됩니다.**

UF2 경로는 보드를 대신 리셋해 줄 수 없습니다. 대용량 저장 볼륨은 더블탭에만
올라오고 1200bps 터치는 평범한 CDC 모드로 들어가기 때문입니다.

UF2 변환은 마이크로소프트의 `uf2conv.py` 를 가져다 쓰지 않고 `baramdl` 안에
넣었습니다. 아두이노 IDE 는 파이썬을 함께 배포하지 않으므로, 파이썬이 필요한
후처리 단계는 윈도우에서 그냥 실패합니다.

업로드는 242KB 이미지 기준 약 5.5초입니다. 그중 전송이 0.9초이고 나머지는
QSPI 지우기입니다.

보드를 추가하실 때 알아두면 좋은 것 셋입니다.

- **업로드 방법마다 `upload.protocol` 이 반드시 있어야 합니다.** arduino-cli 는
  도구를 `upload.tool.<protocol>` 로 찾는데, protocol 이 없으면 이름을 만들지
  못하고 `upload.tool.default` 로도 못 갑니다. 그러면 "A programmer is required
  to upload" 가 뜨는데, **도구 이름이 틀렸을 때와 메시지가 같아서** 오진하기
  쉽습니다.
- **arduino-cli 가 넘기는 포트는 1200bps 터치 이전의 것입니다.** 도구가 실행될
  때쯤이면 보드는 이미 재부팅해서 다른 이름으로 돌아와 있습니다. 그래서
  `baramdl` 은 `--port` 를 힌트로만 쓰고 USB ID 로 부트로더를 직접 찾습니다.
  같은 이유로 이 패키지는 `use_1200bps_touch` 와 `wait_for_upload_port` 를
  꺼둡니다 — 켜두면 arduino-cli 가 아직 열리지 않은 포트를 넘겨 실패합니다.
- **IDE 콘솔에서는 캐리지리턴 진행률이 동작하지 않습니다.** `\r` 을 해석하지
  않고 개행이 올 때까지 줄을 붙들고 있어서, 한 줄이 스스로 갱신되는 표시는
  거기서 불가능합니다. `baramdl` 은 stdout 이 터미널인지 보고, 터미널이면
  제자리에서 막대를 다시 그리고 아니면 10% 마다 한 줄씩 그립니다.

USB 식별자는 전부 [pid.codes](https://pid.codes) 의 `0x1209` 아래입니다.

| PID | |
|---|---|
| `0xB750` | 부트로더, CDC + HID |
| `0xB751` | 부트로더, UF2 대용량 저장 볼륨 포함 |
| `0xB752` | 스케치 |
| `0xB753` ~ `0xB755` | HiGenis 그룹 (자리표시자, 같은 역할 배치) |

### 부트로더 굽기

부트로더 이미지는 `stm32/bootloaders/` 에 들어 있습니다.
*Tools > Programmer* 에서 방법을 고르고 *Tools > Burn Bootloader* 입니다.

- **USB DFU (STM32 시스템 부트로더)** — SW1(BOOT0)을 누른 채 SW3(NRST)를 눌렀다
  떼고 SW1 을 뗍니다. 보드가 `0483:df11` 로 열거됩니다. 따로 설치할 것이 없습니다.
  **다 구운 뒤에는 NRST 를 한 번 눌러 주십시오** — dfu-util 이 USB 를 리셋해도
  BOOT0 로 들어간 DFU 모드는 그대로 남습니다.

  윈도우에서는 이 패키지가 `dfu-util` 을 직접 배포합니다. STM32Tools 가 함께
  주는 윈도우 판본이 2007 년 것이라 주소 지정(`-s`)을 아예 모르기 때문입니다.
  macOS 와 리눅스는 STM32Tools 것을 그대로 씁니다. 자세한 것은
  `stm32/tools/dfu-util/README.md` 를 보십시오.
- **ST-LINK (SWD)** — 플래시에 무엇이 들어 있든 동작하므로 이쪽이 복구 경로입니다.

### WeActH750 라이브러리

객체 하나가 보드의 하드웨어를 소유하므로, 외부 라이브러리 없이 보드를 시험할
수 있습니다.

```cpp
#include <WeActH750.h>

void setup() {
  board.begin(115200);
}

void loop() {
  if (board.lcd.available()) {
    board.lcd.clear(black);
    board.lcd.printf(6, 4, white, "안녕하세요");        // UTF-8
    board.lcd.printfResize(6, 30, green, 32.0f, "BIG"); // 높이 32px
    board.lcd.update();
  }
  board.ledToggle();
  if (board.keyPressed()) board.enterBootloader();
  delay(50);
}
```

LCD 드라이버와 폰트, 한글 조합기는 부트로더의 것을 그대로 가져왔습니다.
`src/` 를 그쪽 include 루트와 같은 배치로 두어서, 두 프로젝트 사이를 오가는
파일이 수정 없이 옮겨집니다. 새로 쓴 것은 `gpio.cpp` 와 `spi.cpp` 뿐이고,
부트로더의 `gpioPinWrite()` 와 `spiXxx()` 를 아두이노 위에 올리는 역할입니다.
그래서 스케치는 부트로더 스플래시 화면과 똑같은 것을 그립니다.

한글은 음절 하나에 비트맵 하나를 두지 않고 초성·중성·종성을 그릴 때 조합합니다.
음절이 11,172 개이므로, 조합하면 언어 전체가 약 80KB 에 들어갑니다.

프레임은 SPI4 에서 DMA 로 나갑니다. 160x80 한 장이 선로에 실리는 5ms 동안
스케치는 다음 장을 준비할 수 있고, `board.lcd.available()` 은 이전 프레임이
다 나갈 때까지 false 입니다.

카메라는 `board.cam` 입니다. 보드의 DVP 헤더에 OV7725 를 물리면 QQVGA
160x120 RGB565 를 28fps 로 받아 `board.cam.drawTo(board.lcd)` 로 화면에
올립니다.

예제: `BoardTest`, `LcdHelloWorld`, `LcdHangul`, `SdCard`, `Camera`.

### SD 라이브러리

`#include <SD.h>` 로 표준 아두이노 SD API 를 그대로 씁니다 — `SD.begin()`,
`File`, `openNextFile()`. 아래는 이 보드의 SDMMC 소켓을 4비트 모드로,
DMA 와 FatFs 로 구동합니다.

별도 구현일 수밖에 없습니다. 표준 라이브러리는 SPI 로만 이야기하는데 이 핀들은
SPI 를 못 합니다. STM32duino 의 `STM32SD` 는 SDMMC 를 구동하지만 GPLv3 라
파일을 여는 모든 스케치에 그 라이선스가 따라붙습니다. FatFs 는 ChaN 의
관대한 라이선스입니다.

`File` 이 `Stream` 을 상속하므로 `Stream&` 를 받는 라이브러리 — 이미지 리더,
오디오 플레이어, JSON 파서 — 가 이 카드의 파일을 그대로 씁니다.

**한글 파일명이 동작합니다.** FatFs R0.15 를 UTF-8 모드(`FF_LFN_UNICODE 2`)와
코드페이지 949 로 씁니다. 소스에 UTF-8 리터럴을 그대로 쓰면 됩니다.

```cpp
File f = SD.open("/한글이름.txt", FILE_WRITE);
```

**exFAT 도 켜져 있습니다.** 64GB 이상 microSD 는 공장에서 exFAT 으로 출하되는데,
그것 없이는 마운트에 실패해 사용자에게 "카드 없음" 으로만 보입니다.

코드페이지 949 테이블 때문에 SD 를 실제로 쓰는 스케치는 약 136KB 커집니다.
`--gc-sections` 가 안 쓰는 쪽을 통째로 걷어가므로 SD 를 안 쓰는 스케치는
영향이 없습니다.

드라이버는 보드마다 갈아끼울 수 있는 구조입니다.

```
hw/driver/sd.h            sd_driver_t. 블록 주소와 개수만 말하고 버스를 모른다
hw/driver/sd.c            디스패처
hw/driver/sd/sd_sdmmc.c   SDMMC 백엔드
```

소켓이 SPI 인 보드는 `sd/sd_spi.c` 를 두고 `sdSelectDriver()` 에 한 줄 더하면
되고, 위층은 손대지 않습니다. **SD 소켓이 없는 보드는 `_USE_HW_SD` 를 정의하지
않으면 됩니다** — 빌드가 되고 플래시를 한 바이트도 쓰지 않습니다.

카드 검출은 배선되어 있지 않습니다. PD4 가 솔더브리지 SB2 를 통해서만 소켓
스위치에 닿는데 SB2 가 열려 있어 핀이 떠 있습니다. `hw_def.h` 는 카드가 있다고
가정하고, 빈 슬롯은 `sdInit()` 이 실패하는 것으로 보고합니다.

**알려진 문제:** 디렉터리 순회가 간헐적으로 빈 목록을 돌려줍니다
(`SdCard.ino` 기준 8회 중 3회). 마운트와 용량은 정상이고 폴트도 나지 않으며,
읽기와 쓰기는 스트레스 시험에서 326MB 무오류였습니다. 원인 미상입니다.

### Serial 은 USB CDC 입니다

`Serial` 이 USB CDC 라서 UART 와 다르게 동작합니다. **호스트가 포트를 열기 전에
출력한 것은 버퍼링되지 않고 사라집니다.** 업로드 직후에는 보드가 다른 USB ID 로
다시 열거되므로 호스트가 포트를 여는 데 시간이 걸립니다. 그래서 `setup()` 의 앞부분
출력이 안 보이는 일이 생깁니다.

네이티브 USB 를 쓰는 아두이노 보드들이 쓰는 관용구를 그대로 쓰면 됩니다.

```cpp
void setup() {
  board.begin(115200);

  // 호스트가 포트를 열 때까지, 최대 2초
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000; ) {
  }

  Serial.println("여기부터는 보입니다");
}
```

`Serial` 의 `operator bool()` 이 호스트가 DTR 을 세웠는지 알려줍니다. 타임아웃을
두는 이유는 **보드를 단독으로 돌릴 때는 포트가 영영 열리지 않기 때문**입니다.
`delay(2000)` 으로도 되지만, 그건 터미널이 이미 붙어 있어도 항상 2초를 기다립니다.

`board.begin()` 은 이 대기를 하지 않습니다. 라이브러리가 조용히 2초를 먹으면
시리얼을 안 쓰는 스케치까지 느려지기 때문입니다. `BoardTest` 와 `Camera` 예제에
들어 있습니다.

`Serial1`(USART1, PA9/PA10)은 진짜 UART 이므로 이 문제가 없습니다.

### 핀맵

```
LED          PE3    active HIGH
버튼 K1      PC13   누르면 HIGH. 내부 풀다운이 필요하다
Serial       PA9 TX / PA10 RX   (USART1)
USB OTG_FS   PA11 DM / PA12 DP  (VBUS 는 MCU 에 연결되어 있지 않다)
QSPI         PB2 CLK, PB6 NCS, PD11 IO0, PD12 IO1, PE2 IO2, PD13 IO3
LCD (SPI4)   PE12 SCK, PE14 MOSI, PE13 DC, PE11 CS, PE10 BL (active LOW)
microSD      PC8-PC11 D0-D3, PC12 CK, PD2 CMD  (SDMMC1)
카메라(DCMI) PC6/PC7/PE0/PE1/PE4/PE5/PE6/PD3 D0-D7, PB7 VSYNC,
             PA4 HSYNC, PA6 PIXCLK, PA8 XCLK(MCO1), PB8/PB9 SCCB
SWD          PA13 / PA14
```

## 배포

```sh
extras/make_release.sh 0.1.0 --dry-run   # 아카이브를 만들고 인덱스를 갱신
extras/make_release.sh 0.1.0             # GitHub 릴리스로 업로드까지
```

`stm32/platform.txt` 의 버전과 인덱스 항목을 맞춰 주고, 그다음
`package_baram_stm32_index.json` 을 커밋해 푸시하면 됩니다.

번들된 부트로더의 README 표는 바이너리에서 직접 만듭니다.

```sh
extras/sync_bootloader_readme.py
```

## 관련 저장소

- **부트로더 펌웨어: [chcbaram/weact-h750-mini](https://github.com/chcbaram/weact-h750-mini)**
  — 이 보드의 부트로더와 앱 펌웨어. `variant_WEACT_H750_MINI.cpp` 의
  `firm_ver_t` 는 그쪽 `src/common/def.h` 의 것과 바이트 단위로 같아야 합니다.
  메모리 맵, VTOR, QSPI 클럭, UF2 규약이 전부 두 저장소에 걸쳐 있습니다.
- 상위 코어: [stm32duino/Arduino_Core_STM32](https://github.com/stm32duino/Arduino_Core_STM32)

## 라이선스

코어와 variant, system 파일은 원래 라이선스를 그대로 따릅니다.
`stm32/License.md` 를 보십시오.
