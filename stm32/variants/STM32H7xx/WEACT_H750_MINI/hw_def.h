#pragma once

/*
 * Board definitions: what is on this board and where.
 *
 * This is the file to edit for a different board: pin names, panel size, panel
 * model. src/ is laid out the way the bootloader's source tree is - hw/hw_def.h
 * for the board, hw/driver/ for what runs on it - so a file moved between the
 * two projects only needs its include prefix adjusted.
 *
 * The driver, its fonts and the Hangul composer are used unmodified - that is
 * the point of this shim. Only the layer below them is different, so a sketch
 * renders exactly what the bootloader's splash screen does.
 *
 * Features the driver can optionally use (CLI, logging, PWM backlight, LVGL)
 * are deliberately left undefined, which compiles them out.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// The SD driver works through the HAL directly, so its types have to be here.
// stm32_def.h is the core's own way in and pulls the right family headers.
#include "stm32_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _DEF_LOW      0
#define _DEF_HIGH     1

// From the bootloader's def.h. constrain() is also an Arduino macro, but this
// file is included by C sources that do not see Arduino.h.
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#define cmax(a, b) (((a) > (b)) ? (a) : (b))
#define cmin(a, b) (((a) < (b)) ? (a) : (b))

// The drivers log and print through these. A sketch has no console guaranteed
// to exist, so they are compiled away.
#define logPrintf(...)     do { } while (0)
#define cmdifPrintf(...)   do { } while (0)

#define _DEF_SPI1     0

#define _USE_HW_GPIO
#define _USE_HW_LCD
#define _USE_HW_ST7735

// microSD on SDMMC1, 4 bit wide.
#define _USE_HW_SD
#define _USE_HW_FATFS

/*
 * Which SDMMC block and which pins. The driver reads these rather than naming
 * pins itself, so a board that wires the socket differently only edits this
 * file - the same rule the rest of hw_def.h follows.
 *
 * All six pins are AF12 on this part. D0-D3 are PC8-PC11, CK is PC12 and CMD is
 * PD2.
 */
#define HW_SD_INSTANCE            SDMMC1
#define HW_SD_IRQn                SDMMC1_IRQn
#define HW_SD_IRQHandler          SDMMC1_IRQHandler
#define HW_SD_CLK_ENABLE()        __HAL_RCC_SDMMC1_CLK_ENABLE()
#define HW_SD_CLK_DISABLE()       __HAL_RCC_SDMMC1_CLK_DISABLE()
#define HW_SD_FORCE_RESET()       __HAL_RCC_SDMMC1_FORCE_RESET()
#define HW_SD_RELEASE_RESET()     __HAL_RCC_SDMMC1_RELEASE_RESET()
#define HW_SD_AF                  GPIO_AF12_SDIO1

#define HW_SD_DATA_PORT           GPIOC
#define HW_SD_DATA_PINS           (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12)
#define HW_SD_DATA_CLK_ENABLE()   __HAL_RCC_GPIOC_CLK_ENABLE()

#define HW_SD_CMD_PORT            GPIOD
#define HW_SD_CMD_PINS            (GPIO_PIN_2)
#define HW_SD_CMD_CLK_ENABLE()    __HAL_RCC_GPIOD_CLK_ENABLE()

/*
 * Card detect is not usable as the board ships.
 *
 * The socket's switch reaches PD4 only through solder bridge SB2, which is
 * open, and there is no external pull either way. Measured with a card in the
 * slot: PD4 reads high with the internal pull-up and low with the pull-down, so
 * it simply follows whichever pull is enabled - the pin is floating.
 *
 * So a card is assumed present and sdInit() failing is what reports an empty
 * slot. Closing SB2 makes detection real: swap the define below for the block
 * under it, and check the polarity - a card in should pull PD4 one way
 * regardless of the internal pull.
 */
#define HW_SD_DETECT_NONE

#if 0
#define HW_SD_DETECT_PORT         GPIOD
#define HW_SD_DETECT_PIN          GPIO_PIN_4
#define HW_SD_DETECT_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define HW_SD_DETECT_PRESENT      GPIO_PIN_RESET
#endif

#define HW_LCD_WIDTH      160
#define HW_LCD_HEIGHT     80
#define HW_LCD_LVGL       0

// Selects the 160x80 panel's offsets (col 1, row 26) and inverted colour mode.
#define HW_ST7735_MODEL   0

// GpioPinName_t in the bootloader. Only the LCD pins matter here, and the
// numbering has to match the table in gpio_shim.c.
typedef enum {
  LCD_CS = 0,
  LCD_DC,
  LCD_BL,
  GPIO_PIN_MAX
} GpioPinName_t;

// The driver calls these directly; Arduino provides them but not to C files
// that do not include Arduino.h.
void     delay(uint32_t ms);
uint32_t millis(void);

#ifdef __cplusplus
}
#endif
