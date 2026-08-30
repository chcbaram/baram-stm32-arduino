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

// 320 x 240 x 2 bytes. D2 SRAM holds 288 KB and the panel's own frame buffer
// takes 25 KB of it, so this fits with room to spare. A second buffer would
// not, which is why the sensor writes into one and the panel reads from it.
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
  Serial.printf("SCCB answer : %s (expecting 0x%02X)\n",
                i2cIsDeviceReady(_DEF_I2C1, OV7725_SLV_ADDR) ? "yes" : "NO",
                OV7725_SLV_ADDR);

  bool sda = false, scl = false;
  i2cBusLevels(_DEF_I2C1, &sda, &scl);
  Serial.printf("bus idle    : SDA %s  SCL %s\n", sda ? "high" : "LOW", scl ? "high" : "LOW");

  uint8_t found[8];
  uint8_t n = i2cScan(_DEF_I2C1, found, sizeof(found));
  Serial.printf("i2c scan    : %u device(s)", n);
  for (uint8_t i = 0; i < n; i++) Serial.printf("  0x%02X", found[i]);
  Serial.println();
  /*
   * Whatever answered, ask it who it is.
   *
   * An OV7725 reports its id straight from 0x0A. An OV2640 has banked
   * registers and has to be pointed at the sensor bank first (0xFF = 1), so
   * both are tried and printed - the pair identifies the part without guessing
   * from the address alone.
   */
  for (uint8_t i = 0; i < n; i++) {
    uint8_t raw = 0, pidh = 0, pidl = 0;
    i2cReadByte2(_DEF_I2C1, found[i], 0x0A, &raw, 100);
    i2cWriteByte2(_DEF_I2C1, found[i], 0xFF, 0x01, 100);   // OV2640 sensor bank
    i2cReadByte2(_DEF_I2C1, found[i], 0x0A, &pidh, 100);
    i2cReadByte2(_DEF_I2C1, found[i], 0x0B, &pidl, 100);
    Serial.printf("id @0x%02X    : raw 0x%02X   banked 0x%02X%02X\n",
                  found[i], raw, pidh, pidl);
  }

  Serial.printf("camera      : %s\n", cam_ok ? "running" : "FAILED");
  Serial.printf("frame seen  : %s\n", cameraIsAvailble() ? "yes" : "not yet");
  Serial.printf("buf sig     : 0x%08lX\n", (unsigned long)bufSignature());
}

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  cam_ok = cameraInit();

  if (cam_ok) {
    cameraSetPixformat(PIXFORMAT_RGB565);
    cameraSetFramesize(FRAMESIZE_QVGA);
    // Circular DMA: the sensor keeps refilling this buffer on its own, so
    // nothing has to re-arm it per frame.
    cam_ok = cameraStart((uint8_t *)cam_buf, CAMERA_MODE_CONTINUOUS);
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
