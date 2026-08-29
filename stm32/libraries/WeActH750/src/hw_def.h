#pragma once

/*
 * Board definitions: what is on this board and where.
 *
 * This is the file to edit for a different board - pin names, panel size, panel
 * model - and it is deliberately kept out of port/, which holds only the
 * adapters between the bootloader's drivers and the Arduino platform. The name
 * matches the bootloader's own hw_def.h so that a driver copied across keeps
 * including the same thing.
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

// The driver logs through this. Sketches have no console guaranteed to exist,
// so it is compiled away.
#define logPrintf(...)   do { } while (0)

#define _DEF_SPI1     0

#define _USE_HW_GPIO
#define _USE_HW_LCD
#define _USE_HW_ST7735

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
