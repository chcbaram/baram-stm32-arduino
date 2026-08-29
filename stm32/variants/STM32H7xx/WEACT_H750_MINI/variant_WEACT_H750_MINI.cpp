/*
 *******************************************************************************
 * Copyright (c) 2020-2021, STMicroelectronics
 * All rights reserved.
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */

/* Dedicated variant directory: no board-name guard needed. */
#include "pins_arduino.h"

/*
 * Digital PinName array.
 *
 * The six QSPI pins are deliberately NC. The sketch executes in place from the
 * QSPI flash, so those pins carry the instruction stream - a single
 * pinMode(5, OUTPUT) would take BK1_NCS off AF10 and stop the CPU mid-fetch.
 * Listing them as NC keeps every other pin number unchanged while making
 * pinMode(), digitalWrite() and digitalRead() no-ops on them, because the core
 * checks for NC before touching the pin.
 */
const PinName digitalPin[] = {
  PE_1,   // D0
  PE_0,   // D1
  PB_9,   // D2
  PB_8,   // D3
  PB_7,   // D4
  NC,     // D5  - PB6, QSPI BK1_NCS: see note below
  PB_5,   // D6
  PB_4,   // D7
  PB_3,   // D8
  PD_7,   // D9
  PD_6,   // D10
  PD_5,   // D11
  PD_4,   // D12
  PD_3,   // D13
  PD_2,   // D14
  PD_1,   // D15
  PD_0,   // D16
  PC_12,  // D17
  PC_11,  // D18
  PC_10,  // D19
  PA_15,  // D20
  PA_12,  // D21
  PA_11,  // D22
  PA_10,  // D23
  PA_9,   // D24
  PA_8,   // D25
  PC_9,   // D26
  PC_8,   // D27
  PC_7,   // D28
  PC_6,   // D29
  PD_15,  // D30
  PD_14,  // D31
  NC,     // D32 - PD13, QSPI IO3: see note below
  NC,     // D33 - PD12, QSPI IO1: see note below
  NC,     // D34 - PD11, QSPI IO0: see note below
  PD_10,  // D35
  PD_9,   // D36
  PD_8,   // D37
  PB_15,  // D38
  PB_14,  // D39
  PB_13,  // D40
  PB_12,  // D41
  NC,     // D42 - PE2, QSPI IO2: see note below
  PE_3,   // D43
  PE_4,   // D44
  PE_5,   // D45
  PE_6,   // D46
  PC_13,  // D47
  PC_0,   // D48/A0
  PC_1,   // D49/A1
  PC_2_C, // D50/A2
  PC_3_C, // D51/A3
  PA_0,   // D52/A4
  PA_1,   // D53/A5
  PA_2,   // D54/A6
  PA_3,   // D55/A7
  PA_4,   // D56/A8
  PA_5,   // D57/A9
  PA_6,   // D58/A10
  PA_7,   // D59/A11
  PC_4,   // D60/A12
  PC_5,   // D61/A13
  PB_0,   // D62/A14
  PB_1,   // D63/A15
  NC,     // D64 - PB2, QSPI CLK: see note below
  PE_7,   // D65
  PE_8,   // D66
  PE_9,   // D67
  PE_10,  // D68
  PE_11,  // D69
  PE_12,  // D70
  PE_13,  // D71
  PE_14,  // D72
  PE_15,  // D73
  PB_10,  // D74
  PB_11,  // D75
  PA_13,  // D76
  PA_14,  // D77
  PC_14,  // D78
  PC_15,  // D79
  PH_0,   // D80
  PH_1    // D81
};

// Analog (Ax) pin number array
const uint32_t analogInputPin[] = {
  48, // A0,  PC0
  49, // A1,  PC1
  50, // A2,  PC2_C
  51, // A3,  PC3_C
  52, // A4,  PA0
  53, // A5,  PA1
  54, // A6,  PA2
  55, // A7,  PA3
  56, // A8,  PA4
  57, // A9,  PA5
  58, // A10, PA6
  59, // A11, PA7
  60, // A12, PC4
  61, // A13, PC5
  62, // A14, PB0
  63  // A15, PB1
};

/*
 * Firmware image header.
 *
 * Placed in the .version output section, which the linker script maps to
 * 0x90001400. The bootloader looks for the "VER " magic there; if it finds one
 * with a non-zero firm_size it computes the CRC over the image itself and
 * promotes it to a verified TAG on first boot. That is why no post-build
 * tagging tool is needed - the CRC cannot be embedded in the image it covers,
 * but the size is known to the linker, and the bootloader exploits that
 * asymmetry.
 *
 * This struct must stay byte-identical to firm_ver_t in the bootloader's
 * src/common/def.h. If that changes, change it here too.
 */
extern uint32_t _fw_flash_begin;
extern uint32_t _fw_flash_size;   /* linker-computed size, read via its address */

typedef struct {
  uint32_t magic_number;
  char     version_str[32];
  char     name_str[32];
  uint32_t firm_addr;
  uint32_t firm_size;
} firm_ver_t;

__attribute__((section(".version"), used))
volatile const firm_ver_t firm_ver = {
  .magic_number = 0x56455220,           /* "VER " */
  .version_str  = "ARDUINO",
  .name_str     = "WEACT-H750-MINI",
  .firm_addr    = (uint32_t)&_fw_flash_begin,
  .firm_size    = (uint32_t)&_fw_flash_size,
};

/**
  * @brief  System Clock Configuration
  *
  * This board does NOT configure the system clock. The application executes in
  * place from the external QSPI flash, and the bootloader has already brought
  * everything up before jumping here. Re-running a normal SystemClock_Config()
  * would break execution in two separate ways:
  *
  *  1. PLL2 must never be touched. The QSPI kernel clock is PLL2R, and
  *     HAL_RCCEx_PeriphCLKConfig() unconditionally calls RCCEx_PLL2_Config()
  *     for any peripheral that selects a PLL2 source - which starts with
  *     __HAL_RCC_PLL2_DISABLE(). That stops the clock feeding the very memory
  *     the CPU is fetching instructions from. The bootloader deliberately puts
  *     QSPI on PLL2 (not PLL1) so that SYSCLK can change without disturbing
  *     XiP; the flip side is that PLL2 itself is off limits.
  *
  *  2. PLL1 cannot be changed anyway. HAL_RCC_OscConfig() skips the whole PLL
  *     block when SYSCLK is already sourced from PLL1 (stm32h7xx_hal_rcc.c,
  *     "Check if the PLL is used as system clock or not"), and it is - the
  *     bootloader hands over running at 400 MHz. Settings written here would be
  *     silently ignored, so SYSCLK is the bootloader's responsibility. If a
  *     different SYSCLK is needed, change it in the bootloader; nothing here
  *     needs to change.
  *
  * State guaranteed by the bootloader at entry (bsp.c):
  *   PWR_LDO_SUPPLY, VOS1, FLASH_LATENCY_2
  *   HSE 25 MHz ON, LSE 32.768 kHz ON (backup write access enabled), HSI48 ON
  *   PLL1 HSE M=5 N=160 P=2 Q=8 R=2  -> VCO 800, SYSCLK 400 MHz, Q 100 MHz
  *   PLL2      M=5 N=80  P=2 Q=2 R=2 -> VCO 400, R 200 MHz = QSPI kernel
  *   PLL3 unused - free for the application
  *   AHB DIV2 (200 MHz), APB1/2/3/4 DIV2 (100 MHz)
  *   QSPI memory mapped, prescaler 1 -> SCK 100 MHz
  *   USB kernel HSI48, RTC LSE, D2SRAM1/2/3 clocked, I+D cache on, MPU active
  *
  * @param  None
  * @retval None
  */
WEAK void SystemClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {};

  /* Pick up the clock tree the bootloader left us so that SystemCoreClock,
   * and with it delay()/micros()/baud rate division, is correct. */
  SystemCoreClockUpdate();

  /* PLL3 is ours alone. Drive it from HSE (25 MHz) and give the peripherals
   * that want a dedicated kernel clock something sane to use.
   * M=5 -> 5 MHz ref, N=32 -> 160 MHz VCO, /2 -> 80 MHz on P, Q and R. */
  PeriphClkInitStruct.PLL3.PLL3M = 5;
  PeriphClkInitStruct.PLL3.PLL3N = 32;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 2;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;   /* 4-8 MHz ref */
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;

  /*
   * Only sources that cannot disturb the bootloader's clock tree:
   *   PLL3     - unused by the bootloader, ours to program
   *   PLL1 Q   - HAL only enables the DIVQ output, it does not reconfigure PLL1
   *   HSI48    - already running
   * Deliberately absent:
   *   RCC_PERIPHCLK_QSPI - the bootloader owns it, see above
   *   anything *_PLL2    - would stop the clock we are executing from
   *   RCC_PERIPHCLK_RTC  - HAL_RCCEx_PeriphCLKConfig() forces a backup domain
   *                        reset when RTCSEL differs, which would wipe the RTC
   *                        backup registers the bootloader keeps its boot mode
   *                        flag, reset count and fault count in. It is already
   *                        on LSE.
   * U(S)ARTs keep their APB default (100 MHz), which is what the bootloader
   * uses for its own console.
   */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_SDMMC
                                             | RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_I2C123
                                             | RCC_PERIPHCLK_I2C4 | RCC_PERIPHCLK_SPI123
                                             | RCC_PERIPHCLK_SPI45 | RCC_PERIPHCLK_SPI6;

  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;   /* PLL1Q, 100 MHz */
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
  PeriphClkInitStruct.I2c123ClockSelection = RCC_I2C123CLKSOURCE_PLL3;
  PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_PLL3;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL3;
  PeriphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PLL3;
  PeriphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_PLL3;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
    Error_Handler();
  }
}
