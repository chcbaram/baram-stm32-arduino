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
  * The application is entered from the bootloader without a reset, but it does
  * not inherit the bootloader's clock tree. SystemInit() runs before main() and
  * puts the RCC back to its reset state (system_stm32h7xx.c):
  *
  *     RCC->CR  |= RCC_CR_HSION;      // HSI on
  *     RCC->CFGR = 0x00000000U;       // SYSCLK back to HSI
  *     RCC->CR  &= 0xEAF6ED7FU;       // HSE, HSI48, PLL1, PLL2, PLL3 all off
  *
  * So HSE and every PLL have to be set up here, exactly as on a board that
  * boots normally. Note the order in that sequence: SYSCLK moves to HSI before
  * the PLLs are stopped, which is what keeps the CPU alive - and it is also why
  * the guard in HAL_RCC_OscConfig() that skips PLL1 while it is the system
  * clock source does not apply by the time this function runs.
  *
  * What is different on this board is that the code being executed lives in the
  * external QSPI flash, so the QUADSPI kernel clock must never stop:
  *
  *  - The bootloader puts QUADSPI on D1HCLK, which follows SYSCLK. SystemInit()
  *    drops it to HSI's 64 MHz but never stops it, and the reset value of
  *    D1CCIPR.QSPISEL selects D1HCLK anyway, so it survives. RCC_PERIPHCLK_QSPI
  *    is deliberately left out of the selection below - there is nothing to fix.
  *    SCK is D1HCLK/2, so 120 MHz once PLL1 is up, within the W25Q64JV's
  *    133 MHz rating.
  *  - Sourcing QUADSPI from a PLL instead would stop the instruction stream at
  *    the RCC->CR write above, before this function ever runs.
  *
  * RCC_PERIPHCLK_RTC is also left out, and must stay out:
  * HAL_RCCEx_PeriphCLKConfig() forces a backup domain reset whenever RTCSEL
  * differs from what is already set, which would wipe the RTC backup registers
  * where the bootloader keeps its boot mode flag, reset count and fault count.
  * The bootloader has already put the RTC on the LSE.
  *
  * @param  None
  * @retval None
  */
WEAK void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {};

  /* Supply configuration update enable */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /* Scale 0 is the boosted performance mode needed for 480 MHz, and is only
   * available with the LDO regulator. The silicon on this board is revision V,
   * which supports it. */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /* HSE 25 MHz crystal -> PLL1 480 MHz.
   * M=5 gives a 5 MHz reference (VCIRANGE_2 covers 4-8 MHz), N=96 a 480 MHz
   * VCO (VCOWIDE covers 192-836 MHz), P=1 the system clock and Q=10 the
   * 48 MHz that USB and SDMMC want. */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 10;
  RCC_OscInitStruct.PLL.PLLR = 10;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /* SYSCLK 480 MHz, HCLK 240 MHz, APB buses 120 MHz.
   * HCLK is also what QUADSPI divides down for SCK: 240 / 2 = 120 MHz. */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }

  /* PLL3 drives the peripherals that want a dedicated kernel clock.
   * M=15 gives a 1.667 MHz reference (VCIRANGE_0 covers 1-2 MHz), N=96 a
   * 160 MHz VCO (VCOMEDIUM covers 150-420 MHz; VCOWIDE starts at 192 MHz and
   * would not lock here), and /2 puts 80 MHz on P, Q and R.
   * PLL2 is left alone - nothing below asks for it. */
  PeriphClkInitStruct.PLL3.PLL3M = 15;
  PeriphClkInitStruct.PLL3.PLL3N = 96;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 2;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_SDMMC
                                             | RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_I2C123
                                             | RCC_PERIPHCLK_I2C4 | RCC_PERIPHCLK_SPI123
                                             | RCC_PERIPHCLK_SPI45 | RCC_PERIPHCLK_SPI6;

  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;      /* PLL1Q, 48 MHz */
  PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;  /* PLL1Q, 48 MHz */
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
