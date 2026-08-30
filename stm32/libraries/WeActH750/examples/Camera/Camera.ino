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

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  cam_ok = cameraInit();

  Serial.println();
  Serial.println(F("--- camera ---"));
  Serial.printf("HSI48 ready : %s\n", (RCC->CR & RCC_CR_HSI48RDY) ? "yes" : "NO");
  Serial.printf("XCLK edges  : %lu\n", (unsigned long)xclkEdges());
  Serial.printf("SCCB answer : %s\n",
                i2cIsDeviceReady(_DEF_I2C1, OV7725_SLV_ADDR) ? "yes" : "NO");
  Serial.printf("cameraInit  : %s\n", cam_ok ? "ok" : "FAILED");

  if (cam_ok) {
    cameraSetPixformat(PIXFORMAT_RGB565);
    cameraSetFramesize(FRAMESIZE_QVGA);
    // Circular DMA: the sensor keeps refilling this buffer on its own, so
    // nothing has to re-arm it per frame.
    cam_ok = cameraStart((uint8_t *)cam_buf, CAMERA_MODE_CONTINUOUS);
    Serial.printf("cameraStart : %s\n", cam_ok ? "ok" : "FAILED");
  }
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

  board.ledToggle();
  delay(30);
}
