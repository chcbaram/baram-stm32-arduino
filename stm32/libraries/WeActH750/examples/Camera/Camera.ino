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
 * Where in the buffer to read, and how long a row really is.
 *
 * Kept as variables rather than baked in: the sensor's actual line length is
 * the thing in doubt, and being able to try a different stride without
 * rebuilding the maths in two places is worth more than the constant folding.
 */
static int32_t cam_stride = HW_CAMERA_WIDTH;
static int32_t cam_x = 0;
static int32_t cam_y = 0;

// Sensor configuration, read back once during bring-up.
static uint8_t reg_im, reg_byp, reg_zw, reg_zh, reg_zhh, reg_hs, reg_vs, reg_c7;

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
  extern uint8_t ov2640_clkrc_before, ov2640_clkrc_after;
  Serial.printf("CLKRC       : 0x%02X -> 0x%02X  (bit7 = PCLK doubler)\n",
                ov2640_clkrc_before, ov2640_clkrc_after);
  Serial.printf("IMAGE_MODE  : 0x%02X (0x08=RGB565 0x00=YUV422 +0x10=JPEG)  R_BYPASS 0x%02X\n",
                reg_im, reg_byp);
  Serial.printf("out window  : ZMOW %u ZMOH %u ZMHH 0x%02X -> %u x %u   HSIZE8 %u VSIZE8 %u   COM7 0x%02X\n",
                reg_zw, reg_zh, reg_zhh, (unsigned)(reg_zw << 2), (unsigned)(reg_zh << 2),
                reg_hs, reg_vs, reg_c7);
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
    // Bring-up aid: set to 1 to have the sensor emit its colour bar instead of
    // a picture, which separates a bad capture path from a bad sensor setup.

    // Bring-up aid: the sensor's own colour bar. Enabled before the registers
    // are read back, so the report shows whether it actually took.
    cameraSetColorbar(1);

    /*
     * What the sensor is actually set to, read back rather than assumed.
     *
     * IMAGE_MODE says which of YUV422 / RGB565 / JPEG the DSP is emitting -
     * getting that wrong makes a perfectly good capture look like noise, and it
     * is not visible from this side any other way. ZMOW/ZMOH/ZMHH are the
     * output window the DSP was told to produce, in units of four pixels.
     */
    {
      uint8_t im = 0, byp = 0, zw = 0, zh = 0, zhh = 0, hs = 0, vs = 0, c7 = 0;
      i2cWriteByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xFF, 0x00, 100);   // DSP bank
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xDA, &im,  100);    // IMAGE_MODE
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0x05, &byp, 100);    // R_BYPASS
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0x5A, &zw,  100);    // ZMOW
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0x5B, &zh,  100);    // ZMOH
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0x5C, &zhh, 100);    // ZMHH
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xC0, &hs,  100);    // HSIZE8
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xC1, &vs,  100);    // VSIZE8
      i2cWriteByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xFF, 0x01, 100);   // sensor bank
      i2cReadByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0x12, &c7,  100);    // COM7
      i2cWriteByte2(_DEF_I2C1, OV2640_SLV_ADDR, 0xFF, 0x00, 100);   // leave in DSP

      // Kept for the periodic report: reading them once is harmless, but doing
      // it repeatedly means writing the bank select every time, which leaves
      // the sensor pointed at the wrong bank and stops it delivering frames.
      reg_im = im; reg_byp = byp; reg_zw = zw; reg_zh = zh; reg_zhh = zhh;
      reg_hs = hs; reg_vs = vs; reg_c7 = c7;
    }

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
       * Straight copy, no scaling.
       *
       * Scaling hides what is wrong: if the number of pixels per line in the
       * buffer is not what the stride says, every row lands shifted and the
       * result looks like noise no matter how good the capture was. A 1:1 crop
       * of the top left shows the raw truth - a real picture appears as a
       * picture, and a wrong stride shows as a recognisable image sheared
       * across the screen rather than as mush.
       */
      const int32_t lw = board.lcd.width();
      const int32_t lh = board.lcd.height();
      uint16_t *dst = board.lcd.frameBuffer();

      for (int32_t y = 0; y < lh; y++) {
        const uint16_t *srow = &cam_buf[(y + cam_y) * cam_stride + cam_x];
        uint16_t       *drow = &dst[y * lw];
        for (int32_t x = 0; x < lw; x++) drow[x] = srow[x];
      }
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
