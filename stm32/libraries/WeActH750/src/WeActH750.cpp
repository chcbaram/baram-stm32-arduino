#include "WeActH750.h"
#include <SD.h>

#include <stdarg.h>

WeActH750::WeActH750() : sd(SD) {}

WeActH750 board;

// PE3 drives an NPN through a 1.5K base resistor, so high lights the LED.
static const uint32_t PIN_LED = PE3;
/*
 * K1, and it is wired the opposite way round to the usual button.
 *
 * Schematic sheet KEY&&LED: SW2 connects PC13 to VDD-MCU through R8 330R. There
 * is no external pull-down - the only other thing on the net is D2, an ESD
 * clamp to ground - so the pin floats when the button is up. It needs the
 * internal pull-down, and a press reads HIGH.
 *
 * Configured as a pull-up instead, the pin reads HIGH whether or not the button
 * is pressed, which is exactly what it did before this was checked against the
 * schematic.
 */
static const uint32_t PIN_KEY = PC13;

bool WeActH750::begin(int baud)
{
  bool ret = true;

  if (baud > 0) {
    Serial.begin(baud);
  }

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  led_state = false;

  pinMode(PIN_KEY, INPUT_PULLDOWN);

  if (!lcd.begin()) {
    ret = false;
  } else {
    lcd.backlight(100);
  }

  is_init = ret;
  return ret;
}

void WeActH750::ledOn(void)
{
  digitalWrite(PIN_LED, HIGH);
  led_state = true;
}

void WeActH750::ledOff(void)
{
  digitalWrite(PIN_LED, LOW);
  led_state = false;
}

void WeActH750::ledToggle(void)
{
  led_state = !led_state;
  digitalWrite(PIN_LED, led_state ? HIGH : LOW);
}

bool WeActH750::keyPressed(void)
{
  return digitalRead(PIN_KEY) == HIGH;
}

void WeActH750::enterBootloader(bool massStorage)
{
  // Provided by the variant, which owns the RTC backup register layout the
  // bootloader reads.
  rebootToBootloader(massStorage);
}

void WeActLcd::printf(int x, int y, uint16_t color, const char *fmt, ...)
{
  char buf[256];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  lcdPrintf(x, y, color, "%s", buf);
}

void WeActLcd::printfResize(int x, int y, uint16_t color, float height_px, const char *fmt, ...)
{
  char buf[256];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  lcdPrintfResize(x, y, color, height_px, "%s", buf);
}
