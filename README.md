# baram-stm32-arduino

Arduino board support for BARAM's STM32 boards, derived from
[STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) 2.12.0 and
trimmed to only what these boards need.

## Boards

| Board | MCU | Notes |
|---|---|---|
| **WEACT-H750-MINI** | STM32H750VBT6 | Custom bootloader; the sketch runs from external QSPI flash |
| HiGenis Dummy | — | Placeholder so the HiGenis group appears in the menu |

## Installing

Add **both** of these URLs under *Preferences > Additional boards manager URLs*:

```
https://raw.githubusercontent.com/chcbaram/baram-stm32-arduino/main/package_baram_stm32_index.json
https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
```

The second one is required: this package hosts no toolchain of its own and
depends on the compiler, OpenOCD, STM32Tools and CMSIS published by
STMicroelectronics. Board Manager downloads them automatically.

Then install **BARAM STM32 Boards** from *Tools > Board > Boards Manager*.

Select the board with:

```
Tools > Board             > BARAM STM32 Boards > BARAM
Tools > Board part number > WEACT-H750-MINI
```

## WEACT-H750-MINI

The STM32H750's 128 KB of internal flash is a single erase sector, so it holds
the bootloader and nothing else. **Sketches are linked for, and execute in
place from, the external QSPI flash** (W25Q64, 8 MB, memory mapped at
`0x90000000`):

```
0x90000000  4K       TAG sector    written by the bootloader after verification
0x90001000  1K       app vectors   VTOR points here
0x90001400  1K       firm_ver_t    the .version section
0x90001800  8M - 6K  app code
```

Three consequences worth knowing about:

- **The bootloader owns the clock tree.** It hands over a running 400 MHz
  system clock, and the variant deliberately does not reconfigure it. PLL2
  feeds the QSPI kernel clock, so touching PLL2 stops the memory the CPU is
  executing from; PLL1 cannot be changed while it is the system clock source
  anyway. PLL3 is free for the application, and the variant puts I2C, SPI and
  the ADC on it. If a different system clock is needed, change it in the
  bootloader - nothing here needs to change.
- **No post-build tagging tool is needed.** The variant emits a `firm_ver_t`
  into `.version`; the bootloader reads the size from it, computes the CRC
  itself and promotes the image to a verified TAG on the first boot.
- **Reflashing the bootloader leaves the sketch alone.** The QSPI contents,
  including the TAG, survive - the board boots straight back into whatever
  application was already there.

### Uploading

USB support defaults to CDC, so a sketch always enumerates and can always be
replaced over the same cable it is powered by. Upload works the way it does on
any other USB-only Arduino board: arduino-cli performs a 1200 bps touch, the
sketch answers it by rebooting into the bootloader, and `tools/baramdl` writes
the new image over the bootloader's CDC interface at about 290 KB/s.

Sketches can trigger the same reboot themselves:

```cpp
rebootToBootloader();       // stay in the bootloader
rebootToBootloader(true);   // and bring up the UF2 mass storage volume
```

If a sketch is built with USB support set to None, or crashes before USB comes
up, **pressing reset twice within 300 ms** keeps the bootloader resident. That
path is handled entirely by the bootloader, so it works no matter what the
sketch does.

| Method | Needs | |
|---|---|---|
| Bootloader USB (CDC) | a USB cable | default, fully automatic |
| UF2 mass storage | a reset double-tap | copies the `.uf2` onto the drive |
| OpenOCD QSPI (SWD) | ST-LINK on PA13/PA14 | `debugger/weact_h750_qspi.cfg`, untested |

Every build also produces a `.uf2` alongside the `.bin`, and *Sketch > Export
Compiled Binary* puts it in the sketch's `build/` folder. Double-tap reset to
bring up the `H750BOOT` drive and drag it across - no tools involved at all.

The UF2 route cannot reset the board for you: the mass storage volume only
appears on a double-tap, while the 1200 bps touch enters plain CDC mode. That is
why the menu entry says so.

The conversion is built into `baramdl` rather than vendored from Microsoft's
`uf2conv.py`, because the Arduino IDE ships no Python and a post-build step that
needs one would fail outright on Windows.

USB identity, all under [pid.codes](https://pid.codes)' `0x1209`:

| PID | |
|---|---|
| `0xB750` | bootloader, CDC + HID |
| `0xB751` | bootloader, with the UF2 mass storage volume |
| `0xB752` | the sketch |

### Burning the bootloader

The bootloader image ships in `stm32/bootloaders/`. Pick the method under
*Tools > Programmer*, then *Tools > Burn Bootloader*:

- **USB DFU (STM32 system bootloader)** — hold SW1 (BOOT0), press and release
  SW3 (NRST), release SW1. The board enumerates as `0483:df11`. Uses the
  `dfu-util` bundled with STM32Tools, so nothing extra to install.
- **ST-LINK (SWD)** — works no matter what is in flash, so this is the recovery
  path.

### Pinout

```
LED          PE3    active HIGH
Button K1    PC13
Serial       PA9 TX / PA10 RX   (LPUART1; the bootloader uses USART1 on the same pins)
USB OTG_FS   PA11 DM / PA12 DP  (VBUS is not wired to the MCU)
QSPI         PB2 CLK, PB6 NCS, PD11 IO0, PD12 IO1, PE2 IO2, PD13 IO3
LCD (SPI4)   PE12 SCK, PE14 MOSI, PE13 DC, PE11 CS, PE10 BL (active LOW)
microSD      PC8-PC11 D0-D3, PC12 CK, PD2 CMD  (SDMMC1)
SWD          PA13 / PA14
```

## Releasing

```sh
extras/make_release.sh 0.1.0 --dry-run   # build the archive, update the index
extras/make_release.sh 0.1.0             # and upload it to a GitHub release
```

It keeps `stm32/platform.txt`'s version and the index entry in step, then you
commit and push `package_baram_stm32_index.json`.

## Related

- Bootloader firmware: `weact-h750-mini` (separate repository). `firm_ver_t` in
  `variant_WEACT_H750_MINI.cpp` must stay byte-identical to the one in its
  `src/common/def.h`.
- Upstream core: [stm32duino/Arduino_Core_STM32](https://github.com/stm32duino/Arduino_Core_STM32)

## License

The core, variants and system files keep their original licenses; see
`stm32/License.md`.
