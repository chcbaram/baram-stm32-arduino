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

/*
 * The camera's frame buffer.
 *
 * In .non_cache, which the linker places in D2 SRAM and the bootloader's MPU
 * maps non-cacheable and 32 byte aligned. That is the reason it lives here
 * rather than in the sketch: the DCMI writes into it by DMA, and a buffer
 * anywhere cacheable would need cleaning and invalidating around every frame.
 *
 * One buffer, not two. QVGA RGB565 is 150 KB and the panel's own frame buffer
 * takes another 25 KB of D2's 288 KB, so a second frame would not fit - and a
 * mixed placement, one here and one in AXI SRAM, would give the two different
 * cache behaviour and put a branch in the capture path for no good reason.
 */
static uint16_t __attribute__((section(".non_cache"), aligned(32)))
  cam_frame[HW_CAMERA_WIDTH * HW_CAMERA_HEIGHT];

bool WeActCamera::begin(framesize_t size)
{
  int32_t w = 0, h = 0;

  if (running) return true;

  if (!cameraInit()) return false;

  if (cameraSetFramesize(size) != 0) return false;

  /*
   * Check the frame fits before arming anything.
   *
   * cameraSetFramesize() refuses a size the sensor does not support and leaves
   * it at the previous one, so the size that matters is the one read back, not
   * the one asked for. Starting a capture larger than the buffer would have the
   * DMA write past the end of it, into whatever .non_cache holds next - which
   * on this board is the panel's own frame buffer.
   */
  if (!cameraGetResolution(&w, &h)) return false;
  if ((uint32_t)(w * h) > (uint32_t)(HW_CAMERA_WIDTH * HW_CAMERA_HEIGHT)) return false;

  // Continuous: the sensor keeps refilling the same buffer on its own, so
  // nothing has to re-arm a transfer per frame.
  running = cameraStart((uint8_t *)cam_frame, CAMERA_MODE_CONTINUOUS);
  return running;
}

bool WeActCamera::available(void)
{
  return running && cameraIsAvailble();
}

uint16_t *WeActCamera::frameBuffer(void)
{
  return cam_frame;
}

int32_t WeActCamera::width(void)
{
  int32_t w = HW_CAMERA_WIDTH;
  cameraGetResolution(&w, NULL);
  return w;
}

int32_t WeActCamera::height(void)
{
  int32_t h = HW_CAMERA_HEIGHT;
  cameraGetResolution(NULL, &h);
  return h;
}

void WeActCamera::drawTo(WeActLcd &lcd)
{
  const int32_t lw = lcd.width();
  const int32_t lh = lcd.height();
  const int32_t cw = width();
  const int32_t ch = height();

  uint16_t *dst = lcd.frameBuffer();
  if (dst == NULL || !running) return;

  // Centre the panel's window in the frame, clamped so a frame smaller than the
  // panel still copies what there is instead of reading past the buffer.
  const int32_t cols = (cw < lw) ? cw : lw;
  const int32_t rows = (ch < lh) ? ch : lh;
  const int32_t x0   = (cw - cols) / 2;
  const int32_t y0   = (ch - rows) / 2;

  for (int32_t y = 0; y < rows; y++) {
    const uint16_t *srow = &cam_frame[(y + y0) * cw + x0];
    uint16_t       *drow = &dst[y * lw];
    for (int32_t x = 0; x < cols; x++) drow[x] = srow[x];
  }
}

void WeActCamera::mirror(bool enable)
{
  cameraSetHmirror(enable ? 1 : 0);
}

void WeActCamera::flip(bool enable)
{
  cameraSetVflip(enable ? 1 : 0);
}

void WeActCamera::colorBar(bool enable)
{
  cameraSetColorbar(enable ? 1 : 0);
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
