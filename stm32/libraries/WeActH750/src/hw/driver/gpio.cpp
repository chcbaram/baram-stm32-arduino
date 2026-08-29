/*
 * gpioPinWrite() for the bootloader's LCD driver, on top of the Arduino pin
 * API. Compiled as C++ so it can see the Arduino pin numbers, and exported with
 * C linkage for the driver to call.
 */

#include <Arduino.h>

extern "C" {
#include "hw/hw_def.h"
#include "hw/driver/gpio.h"
}

/*
 * Indexed by GpioPinName_t, with the polarity table the bootloader's gpio.c
 * carries. gpioPinWrite(ch, true) writes on_level, so the driver can say "on"
 * and let the board decide what that means electrically.
 *
 * CS and DC pass straight through, because st7735.c drives them by explicit
 * level.
 *
 * The backlight is active low. Schematic sheet 06-TFT-LCD: an SI2301 P-channel
 * FET with its source at 3V3 and its gate pulled up to 3V3 through R37 10K. A
 * P-channel device conducts when the gate is below the source, so LCD_LED has
 * to be pulled low to light the panel. Without this inversion lcdSetBackLight()
 * turns the backlight off, which is exactly what it did before this table
 * existed.
 */
struct GpioDef {
  uint32_t pin;
  uint8_t  on_level;   // what gpioPinWrite(ch, true) actually drives
};

static const GpioDef gpio_tbl[GPIO_PIN_MAX] = {
  { PE11, HIGH },  // LCD_CS
  { PE13, HIGH },  // LCD_DC
  { PE10, LOW  },  // LCD_BL, active low
};

static bool gpio_ready = false;

static void gpioBeginOnce(void)
{
  if (gpio_ready) return;
  for (int i = 0; i < GPIO_PIN_MAX; i++) {
    pinMode(gpio_tbl[i].pin, OUTPUT);
  }
  gpio_ready = true;
}

extern "C" void gpioPinWrite(uint8_t ch, bool value)
{
  if (ch >= GPIO_PIN_MAX) return;
  gpioBeginOnce();
  const GpioDef &g = gpio_tbl[ch];
  digitalWrite(g.pin, value ? g.on_level : (g.on_level == HIGH ? LOW : HIGH));
}

extern "C" bool gpioPinRead(uint8_t ch)
{
  if (ch >= GPIO_PIN_MAX) return false;
  gpioBeginOnce();
  const GpioDef &g = gpio_tbl[ch];
  return digitalRead(g.pin) == g.on_level;
}
