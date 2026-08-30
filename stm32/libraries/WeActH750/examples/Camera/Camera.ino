/*
 * WEACT-H750-MINI - the DVP camera, on screen.
 *
 * The camera header is wired to DCMI in 8 bit mode. An OV7725 module delivers
 * QVGA RGB565 straight into memory over DMA, and the sketch scales each frame
 * onto the 160x80 panel.
 *
 * The frame buffer lives in D2 SRAM through the .non_cache section. The MPU
 * maps that region non-cacheable, so nothing here has to clean or invalidate
 * anything around the DMA - which is the reason for putting it there rather
 * than in the larger AXI SRAM.
 *
 * setup() prints what it found over Serial. That matters more than usual here:
 * the sensor clocks its SCCB logic from XCLK, so a missing clock looks exactly
 * like a missing camera. The report separates the two.
 */

#include <WeActH750.h>

extern "C" {
#include "hw/driver/camera.h"
#include "hw/driver/i2c.h"
#include "hw/driver/resize.h"
}

// 160 x 120 x 2 bytes, in D2 SRAM alongside the panel's own frame buffer. Small
// enough that the single-buffer argument no longer bites, but there is still no
// reason for a second one: the panel reads what the sensor is filling.
static uint16_t cam_buf[HW_CAMERA_WIDTH * HW_CAMERA_HEIGHT]
  __attribute__((section(".non_cache"), aligned(32)));

static bool cam_ok = false;

/*
 * Counts edges on the XCLK pin.
 *
 * The pin stays in its alternate function; IDR still reports the level, so
 * sampling it in a tight loop shows whether MCO1 is actually driving. At 12 MHz
 * this returns thousands. Zero means the clock never started, which points at
 * HSI48 or MCO1 rather than at the sensor or the wiring.
 */
static uint32_t xclkEdges(void)
{
  uint32_t prev  = HW_CAMERA_XCLK_PORT->IDR & HW_CAMERA_XCLK_PIN;
  uint32_t edges = 0;

  for (int i = 0; i < 20000; i++) {
    uint32_t now = HW_CAMERA_XCLK_PORT->IDR & HW_CAMERA_XCLK_PIN;
    if (now != prev) edges++;
    prev = now;
  }
  return edges;
}

/*
 * What the bring-up actually found.
 *
 * Printed once at boot and then every couple of seconds, so a terminal opened
 * at any time sees it - the interesting output would otherwise be gone before
 * the port finished enumerating.
 *
 * The buffer signature is the point of the repeat: it sums a scattering of
 * pixels, so a value that changes between reports means DMA is delivering live
 * frames rather than the buffer just being non-zero once.
 */
static uint32_t bufSignature(void)
{
  uint32_t sum = 0;
  for (int i = 0; i < HW_CAMERA_WIDTH * HW_CAMERA_HEIGHT; i += 997) {
    sum += cam_buf[i];
  }
  return sum;
}

static void report(void)
{
  Serial.println();
  Serial.println(F("--- camera ---"));
  Serial.printf("HSI48 ready : %s\n", (RCC->CR & RCC_CR_HSI48RDY) ? "yes" : "NO");
  Serial.printf("XCLK edges  : %lu\n", (unsigned long)xclkEdges());
  // Passive only. Anything that writes to the sensor belongs in setup().
  int32_t w = 0, h = 0;
  cameraGetResolution(&w, &h);
  Serial.printf("resolution  : %ld x %ld\n", (long)w, (long)h);
  extern uint32_t dbg_frame_cnt, dbg_err_cnt, dbg_err_lisr, dbg_err_s0cr, dbg_err_ndtr, dbg_err_ris;
  Serial.printf("frames/errs : %lu / %lu\n",
                (unsigned long)dbg_frame_cnt, (unsigned long)dbg_err_cnt);
  Serial.printf("at error    : LISR 0x%08lX  CR 0x%08lX  NDTR %lu  RIS 0x%02lX\n",
                (unsigned long)dbg_err_lisr, (unsigned long)dbg_err_s0cr,
                (unsigned long)dbg_err_ndtr, (unsigned long)dbg_err_ris);

  uint32_t derr = 0, merr = 0;
  cameraGetError(&derr, &merr);
  // DCMI: 1 OVR, 2 SYNC, 0x40 DMA.  DMA: 1 TE, 2 FE, 4 DME, 0x20 timeout.
  Serial.printf("err dcmi/dma: 0x%02lX 0x%02lX\n",
                (unsigned long)derr, (unsigned long)merr);
  Serial.printf("DCMI SR/RIS : 0x%02lX 0x%02lX\n",
                (unsigned long)DCMI->SR, (unsigned long)DCMI->RISR);
  Serial.printf("DMA NDTR    : %lu\n",
                (unsigned long)HW_CAMERA_DMA_STREAM->NDTR);
  Serial.printf("camera      : %s\n", cam_ok ? "running" : "FAILED");
  Serial.printf("frame seen  : %s\n", cameraIsAvailble() ? "yes" : "not yet");
  Serial.printf("buf sig     : 0x%08lX\n", (unsigned long)bufSignature());
}

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  // Markers, not decoration: cameraInit() walks a long SCCB register table and
  // a stall inside it is otherwise indistinguishable from a sketch that never
  // started. Each line tells the next stage was reached.
  Serial.println(F("\nboot"));
  Serial.flush();

  cam_ok = cameraInit();
  Serial.printf("cameraInit  : %s\n", cam_ok ? "ok" : "FAILED");
  Serial.flush();

  /*
   * Identify whatever is on the bus, once, once the clock is up.
   *
   * Two things put it exactly here. It runs after cameraInit() because the
   * sensor clocks its SCCB logic from XCLK and will not answer - or release the
   * bus - before that exists; a scan of all 112 addresses against a dead bus
   * hangs rather than merely reporting nothing. And it runs once rather than in
   * the repeating report because an OV2640 has banked registers, so reading its
   * id means writing the bank select first, and doing that every couple of
   * seconds leaves the sensor pointed at the wrong bank in between - which
   * stops it delivering frames.
   */
  {
    uint8_t seen[8];
    i2cBegin(_DEF_I2C1, HW_CAMERA_I2C_FREQ / 1000);
    uint8_t m = i2cScan(_DEF_I2C1, seen, sizeof(seen));
    Serial.printf("i2c scan    : %u device(s)", m);
    for (uint8_t i = 0; i < m; i++) Serial.printf("  0x%02X", seen[i]);
    Serial.println();
    for (uint8_t i = 0; i < m; i++) {
      uint8_t pidh = 0, pidl = 0;
      i2cWriteByte2(_DEF_I2C1, seen[i], 0xFF, 0x01, 100);   // OV2640 sensor bank
      i2cReadByte2(_DEF_I2C1, seen[i], 0x0A, &pidh, 100);
      i2cReadByte2(_DEF_I2C1, seen[i], 0x0B, &pidl, 100);
      Serial.printf("id @0x%02X    : 0x%02X%02X\n", seen[i], pidh, pidl);
    }
    Serial.flush();
  }


  if (cam_ok) {
    cameraSetPixformat(PIXFORMAT_RGB565);
    cameraSetFramesize(HW_CAMERA_FRAMESIZE);
    // Circular DMA: the sensor keeps refilling this buffer on its own, so
    // nothing has to re-arm it per frame.
    cam_ok = cameraStart((uint8_t *)cam_buf, CAMERA_MODE_CONTINUOUS);
    Serial.printf("cameraStart : %s\n", cam_ok ? "ok" : "FAILED");
    Serial.flush();
  }

  report();
}

void loop()
{
  if (board.lcd.available())
  {
    if (cam_ok) {
      /*
       * The sensor is 4:3 and the panel is 2:1, so scaling the whole frame
       * would squash it. Take the middle 320x160 band instead and scale that,
       * which keeps circles round at the cost of the top and bottom.
       */
      resize_image_t src = { HW_CAMERA_WIDTH, HW_CAMERA_HEIGHT / 2,
                             0, HW_CAMERA_HEIGHT / 4,
                             HW_CAMERA_WIDTH, cam_buf };
      resize_image_t dst = { board.lcd.width(), board.lcd.height(), 0, 0,
                             board.lcd.width(), board.lcd.frameBuffer() };
      resizeImageFast(&src, &dst);
    } else {
      board.lcd.clear(black);
      board.lcd.printf(4, 2,  red,  "카메라 없음");
      board.lcd.printf(4, 20, gray, "Serial 확인");
    }
    board.lcd.update();
  }

  static uint32_t pre_time = 0;
  if (millis() - pre_time >= 2000) {
    pre_time = millis();
    report();
  }

  board.ledToggle();
  delay(30);
}
