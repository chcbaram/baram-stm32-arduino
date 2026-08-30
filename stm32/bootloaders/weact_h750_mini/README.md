# weact_h750_mini bootloader image

| | |
|---|---|
| version | `V260830R7` |
| name | `WEACT-H750-BOOT` |
| size | 100,232 bytes |
| sha256 | `0692eb9aed6a05ec9840125bf3ef6c9e6930d4f2ea1fab7a497675c82d535620` |

Shipped as both `.bin` and `.hex`. The `.hex` carries its own load addresses,
so it can be handed to a programmer without naming 0x08000000 separately.

Built from the `weact-h750-mini` repository, `firmware/weact-h750-boot`, and
verified on hardware running a 480 MHz Arduino sketch from QSPI.

The version string is not tracked separately: the bootloader carries its own
`firm_ver_t` at `0x08000400`, so it can be read straight out of this file.

```sh
python3 -c "d=open('weact_h750_mini.bin','rb').read(); print(d[0x404:0x424].split(b'\0')[0].decode())"
```

## What it provides

- Boots the application from external QSPI flash at `0x90001000`, classifying
  the image as NONE / RAW / VER / TAG and promoting VER to a CRC-verified TAG
  on the first boot.
- Stays resident on an NRST double-tap (two resets within 300 ms), on request
  from the application via RTC backup register DR3, or when no valid image is
  found. This is the recovery path and works no matter what the application
  does, so a sketch that never brings up USB cannot lock you out.
- Accepts firmware over USB CDC and HID using a packet protocol, and over UF2
  mass storage. `tools/baramdl` speaks the CDC side.
- Leaves QUADSPI memory mapped with its kernel clock on D1HCLK, which is what
  lets the application change SYSCLK without stopping its own instruction
  fetch.
- Sets up the MPU, which the application then inherits - the Arduino core has no
  MPU code of its own. AXI SRAM at `0x24000000` is write-through cacheable and
  execute-never; D2 SRAM at `0x30000000` is non-cacheable, which is what makes
  the `.non_cache` section safe to hand to DMA without maintenance. Both regions
  are read straight off the running target rather than taken on trust; the
  values are in the cache note in the SD driver.

It also refuses to jump after three *consecutive* fault resets, staying resident
with mass storage up rather than looping on a broken image. Sketches need do
nothing for this: the bootloader does not set VTOR before handing over, so its
own fault handler is still installed through the window where a broken image
dies. Writing a new image clears the counter, so an ordinary upload is the way
back out.

Reflashing it does not touch the QSPI flash, so the application and its tag
survive - the board boots straight back into whatever sketch was there.
