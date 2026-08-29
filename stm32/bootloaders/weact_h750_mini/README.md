# weact_h750_mini bootloader image

Built from the `weact-h750-mini` repository, `firmware/weact-h750-boot`.

## Do not burn this build

This image predates two fixes and is kept only as a placeholder:

- The QSPI kernel clock was sourced from PLL2. `SystemInit()` in the
  application clears `PLL2ON` before `main()` runs, which stops the clock the
  CPU is fetching instructions over. It has since moved to D1HCLK, which
  follows SYSCLK and therefore survives.
- `SIOOMode` sent the read opcode on every command while the flash was in
  continuous read mode, so random access returned garbage. Sequential bursts
  still read correctly, which is why block-compare checks passed and only
  instruction fetch failed.

A board flashed with this image cannot start an application from QSPI.
