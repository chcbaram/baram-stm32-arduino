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
 * a file can be copied between the two projects with only its include prefix
 * adjusted. gpio.cpp and spi.cpp are the exception: they are the adapters that
 * put the bootloader's gpioPinWrite() and spiXxx() on the Arduino platform.
 *
 * The board itself is described by hw_def.h, which lives in the variant rather
 * than here - it is per board, and the variant is already on the include path
 * for every compile, which is what lets the SD library share these drivers
 * without depending on this one.
 */

#include <Arduino.h>

extern "C" {
#include "hw/driver/lcd.h"
#include "hw/driver/camera.h"
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

/*
 * The DVP camera on the board's camera header.
 *
 * begin() brings the sensor up and starts it capturing continuously into a
 * frame buffer this class owns, so a sketch never has to place one itself -
 * that placement matters more than it looks. The buffer lives in D2 SRAM
 * through the .non_cache section, which the MPU maps non-cacheable, and that
 * is what lets the capture run with no cache maintenance around the DMA.
 *
 * Pixels are RGB565, the same format the panel takes, so drawTo() is a copy
 * rather than a conversion.
 *
 * A sketch that never touches board.cam pays nothing for it. The frame buffer
 * is a file scope object in WeActH750.cpp reached only from these methods, and
 * the core compiles with -ffunction-sections -fdata-sections and links with
 * --gc-sections, so an uncalled begin() takes its buffer with it: the LCD-only
 * examples come out with 25 KB of .non_cache rather than 175 KB. That holds
 * only while nothing outside these methods refers to the buffer - reaching it
 * from a constructor, or from anything that runs whether or not the camera is
 * used, would anchor all 150 KB into every sketch.
 *
 *   void setup() {
 *     board.begin(115200);
 *     board.cam.begin();
 *   }
 *
 *   void loop() {
 *     if (board.lcd.available()) {
 *       board.cam.drawTo(board.lcd);
 *       board.lcd.update();
 *     }
 *   }
 */
class WeActCamera
{
public:
  /*
   * Finds the sensor, configures it for one frame size and starts capturing.
   * False means nothing answered on the camera header's SCCB bus, or that the
   * size asked for does not fit the frame buffer.
   *
   * The buffer is sized once, at compile time, for HW_CAMERA_WIDTH x
   * HW_CAMERA_HEIGHT in hw_def.h - that is the largest frame this board will
   * capture. A smaller size simply uses part of it, so QQVGA costs the same
   * memory as QVGA; raising the ceiling means editing hw_def.h.
   *
   *   board.cam.begin()                     the board's default size
   *   board.cam.begin(FRAMESIZE_QQVGA)      160x120, a quarter of the data
   */
  bool begin(framesize_t size = HW_CAMERA_FRAMESIZE);

  // True once capture is running.
  bool isRunning(void)                     { return running; }

  // True when a frame has completed since the last time this was asked.
  bool available(void);

  /*
   * Frames per second the sensor is delivering, refreshed about once a second.
   *
   * Counted from the capture interrupt, not from how often a sketch draws: the
   * panel takes about 5 ms a frame and the sensor does not wait for it, so the
   * two numbers are different and the sensor's is the one that says whether the
   * capture is healthy.
   */
  uint32_t fps(void);

  // The most recent frame. RGB565, width() x height().
  uint16_t *frameBuffer(void);
  int32_t width(void);
  int32_t height(void);

  /*
   * Copies the middle of the frame onto the panel, one sensor pixel to one
   * panel pixel.
   *
   * No scaling: the sensor is 4:3 and the panel is 2:1, so a scaled frame would
   * be squashed, and a wrong assumption about the frame's line length would
   * show up as noise rather than as something recognisably shifted. Taking the
   * middle keeps circles round and keeps the failure mode readable.
   */
  void drawTo(WeActLcd &lcd);

  // Left-right and top-bottom, done in the sensor rather than in the copy.
  void mirror(bool enable);
  void flip(bool enable);

  /*
   * The sensor's own colour bar.
   *
   * Bring-up aid: bars on the panel mean the capture, the pixel format and the
   * display are all working, so anything still wrong is the lens, the exposure
   * or the sensor's configuration. It is the fastest way to split those apart.
   */
  void colorBar(bool enable);

private:
  bool     running    = false;
  uint32_t fps_value  = 0;
  uint32_t fps_frames = 0;
  uint32_t fps_time   = 0;
};

class SDClass;

class WeActH750
{
public:
  WeActLcd lcd;
  WeActCamera cam;

  /*
   * The microSD socket, through the SD library's global - a reference, so
   * board.sd and the SD object a sketch or another library uses are the same
   * thing: open a file through one and read it through the other.
   *
   * Only forward declared here. arduino-cli discovers libraries by scanning the
   * sketch's own includes, and an <SD.h> reached only from this header is not
   * found, so a sketch that wants the card writes #include <SD.h> as well. That
   * is also what keeps a sketch with no interest in SD from compiling it.
   *
   * board.begin() does not mount it - a board with an empty slot should still
   * come up. Call board.sd.begin() when a card is wanted.
   */
  SDClass &sd;

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

  WeActH750();

private:
  bool is_init = false;
  bool led_state = false;
};

extern WeActH750 board;
