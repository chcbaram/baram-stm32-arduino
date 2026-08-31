# baram-stm32-arduino

BARAM 의 STM32 보드용 아두이노 보드 패키지.
[STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) 2.12.0 에서
이 보드들에 필요한 것만 추려 만들었습니다.

## 보드

| 보드 | MCU | 문서 | 비고 |
|---|---|---|---|
| **WEACT-H750-MINI** | STM32H750VBT6 | [docs/WEACT-H750-MINI.md](docs/WEACT-H750-MINI.md) | 자체 부트로더. 스케치가 외부 QSPI 플래시에서 실행됩니다 |
| HiGenis Dummy | — | — | HiGenis 그룹이 메뉴에 보이게 하는 자리표시자 |

보드별 상세 — 메모리 맵, 업로드 방법, 부트로더 굽기, 라이브러리, 핀맵 — 는
`docs/` 의 보드 문서에 있습니다. 보드가 늘면 문서도 하나씩 늘어납니다.

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

그다음은 쓰시려는 보드의 문서를 보십시오 —
[WEACT-H750-MINI](docs/WEACT-H750-MINI.md).

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
