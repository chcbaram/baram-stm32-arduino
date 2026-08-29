# weact_h750_mini bootloader image

| | |
|---|---|
| version | `V260829R1` |
| name | `WEACT-H750-BOOT` |
| size | 99,272 bytes |
| sha256 | `383f76f0c83b34548fda8ebe7e1852edb8f2e2ecdc5d4b7056f7e5e1b289263b` |

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

Reflashing it does not touch the QSPI flash, so the application and its tag
survive - the board boots straight back into whatever sketch was there.
