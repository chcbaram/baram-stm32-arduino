# weact_h750_mini bootloader image

| | |
|---|---|
| version | `V260830R3` |
| name | `WEACT-H750-BOOT` |
| size | 99,936 bytes |
| sha256 | `d0faf7bf5b5dc643d4c023a806c24befc7e780c1bf9ef3a5a49c18b5aebaabf0` |

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

It also refuses to jump after three *consecutive* fault resets, staying resident
with mass storage up rather than looping on a broken image. Sketches need do
nothing for this: the bootloader does not set VTOR before handing over, so its
own fault handler is still installed through the window where a broken image
dies. Writing a new image clears the counter, so an ordinary upload is the way
back out.

Reflashing it does not touch the QSPI flash, so the application and its tag
survive - the board boots straight back into whatever sketch was there.
