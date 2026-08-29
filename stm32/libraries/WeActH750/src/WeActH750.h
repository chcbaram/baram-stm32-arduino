#pragma once

/*
 * WEACT-H750-MINI board support.
 *
 * One object owns the on-board hardware, so a sketch can exercise the board
 * without pulling in anything else:
 *
 *   #include <WeActH750.h>
 *
 *   void setup() {
 *     board.begin(115200);
 *     board.lcd.printf(0, 0, white, "안녕하세요");
 *     board.lcd.update();
 *   }
 *
 *   void loop() {
 *     board.ledToggle();
 *     if (board.keyPressed()) { ... }
 *     delay(500);
 *   }
 *
 * The LCD driver, its fonts and the Hangul composer are the bootloader's, used
 * unmodified - only the SPI and GPIO layer underneath is Arduino's. A sketch
 * therefore draws exactly what the bootloader's splash screen does, and a fix
 * on either side is a file copy away.
 *
 * The C API (lcdPrintf, lcdDrawLine, ...) stays available for anyone who wants
 * it; board.lcd is a thin wrapper over the same calls.
 *
 * src/hw/ holds what came from the bootloader, arranged the way it is there, so
 * a file can be copied between the two projects unchanged. Only src/hw/port/
 * is new: the adapters that put the bootloader's spiXxx() and gpioPinWrite()
 * on top of the Arduino platform.
 */

#include <Arduino.h>

extern "C" {
#include "lcd.h"
}

#define WEACT_H750_VER_STR   "WEACT-H750 V260830R1"

/*
 * The 160x80 ST7735S panel.
 *
 * Drawing goes into a framebuffer; update() pushes it to the panel. A whole
 * frame is 25 KB, about 5 ms at 40 MHz, and the SPI transfer blocks for that
 * long - the core's SPI library has no DMA path, and neither does the shim yet.
 *
 * Colours are the names from lcd.h and are RGB565: white, gray, darkgray,
 * black, purple, pink, red, orange, brown, beige, yellow, lightgreen, green,
 * darkblue, blue, lightblue.
 *
 * Text is UTF-8. Only LCD_FONT_HAN can draw Korean; LCD_FONT_07x10,
 * LCD_FONT_11x18 and LCD_FONT_16x26 are ASCII only.
 */
class WeActLcd
{
public:
  bool begin(void)                         { return lcdInit(); }

  int32_t width(void)                      { return lcdGetWidth(); }
  int32_t height(void)                     { return lcdGetHeight(); }

  // 0 turns the backlight off, anything else turns it on. The pin drives a
  // P-channel FET, so the polarity is hidden here.
  void backlight(uint8_t percent)          { lcdSetBackLight(percent); }

  // False while the previous frame is still on its way to the panel.
  bool available(void)                     { return lcdDrawAvailable(); }
  // Hand the framebuffer to the panel.
  bool update(void)                        { return lcdRequestDraw(); }

  void clear(uint32_t color = black)       { lcdClearBuffer(color); }

  void pixel(uint16_t x, uint16_t y, uint32_t c)                 { lcdDrawPixel(x, y, c); }
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) { lcdDrawLine(x0, y0, x1, y1, c); }
  void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c)     { lcdDrawRect(x, y, w, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { lcdDrawFillRect(x, y, w, h, c); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t c)          { lcdDrawFillCircle(x, y, r, c); }
  void fillScreen(uint16_t c)                                           { lcdDrawFillScreen(c); }
  void roundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c)     { lcdDrawRoundRect(x, y, w, h, r, c); }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { lcdDrawFillRoundRect(x, y, w, h, r, c); }
  void triangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t c)     { lcdDrawTriangle(x1, y1, x2, y2, x3, y3, c); }
  void fillTriangle(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t c) { lcdDrawFillTriangle(x1, y1, x2, y2, x3, y3, c); }

  void setFont(LcdFont font)               { lcdSetFont(font); }

  // Same arguments as printf(). UTF-8, so Korean works as-is.
  void printf(int x, int y, uint16_t color, const char *fmt, ...)
      __attribute__((format(printf, 5, 6)));

  // The same font drawn at an arbitrary height in pixels - not a multiplier.
  // The base font is 16 tall, so 32 is double size. Capped at 64.
  void printfResize(int x, int y, uint16_t color, float height_px, const char *fmt, ...)
      __attribute__((format(printf, 6, 7)));

  uint32_t drawTime(void)                  { return lcdGetDrawTime(); }
  uint32_t fps(void)                       { return lcdGetFps(); }

  uint16_t *frameBuffer(void)              { return lcdGetFrameBuffer(); }
};

class WeActH750
{
public:
  WeActLcd lcd;

  // Brings up Serial, the LCD and the on-board LED and button. Returns false
  // if any part failed; the parts that did come up still work.
  bool begin(int baud = 115200);

  // PE3, active high.
  void ledOn(void);
  void ledOff(void);
  void ledToggle(void);

  // K1 on PC13. Pressed is HIGH: the button connects the pin to VDD and the
  // internal pull-down holds it low otherwise. See the schematic note in the .cpp.
  bool keyPressed(void);

  // Reboot into the bootloader so the next sketch can be uploaded. Pass true to
  // also bring up the UF2 mass storage volume. Pressing reset twice within
  // 300 ms does the same thing without any help from the sketch.
  void enterBootloader(bool massStorage = false);

  const char *version(void) { return WEACT_H750_VER_STR; }

private:
  bool is_init = false;
  bool led_state = false;
};

extern WeActH750 board;
