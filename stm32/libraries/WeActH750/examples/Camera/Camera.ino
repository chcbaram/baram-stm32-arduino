/*
 * WEACT-H750-MINI - the DVP camera, on screen.
 *
 * The camera header is wired to DCMI in 8 bit mode. An OV7725 module delivers
 * QVGA RGB565 straight into memory over DMA, and the sketch scales each frame
 * down onto the 160x80 panel.
 *
 * The frame buffer lives in D2 SRAM through the .non_cache section. That region
 * is mapped non-cacheable by the MPU, so nothing here has to clean or
 * invalidate anything around the DMA - which is the whole reason for putting it
 * there rather than in the larger AXI SRAM.
 */

#include <WeActH750.h>

extern "C" {
#include "hw/driver/camera.h"
#include "hw/driver/resize.h"
}

// 320 x 240 x 2 bytes. D2 SRAM holds 288 KB and the panel's own frame buffer
// takes 25 KB of it, so this fits with room to spare. A second buffer would
// not, which is why the sensor writes into one and the panel reads from it.
static uint16_t cam_buf[HW_CAMERA_WIDTH * HW_CAMERA_HEIGHT]
  __attribute__((section(".non_cache"), aligned(32)));

static bool cam_ok = false;

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  cam_ok = cameraInit();
  if (cam_ok) {
    cameraSetPixformat(PIXFORMAT_RGB565);
    cameraSetFramesize(FRAMESIZE_QVGA);
    // Circular DMA: the sensor keeps refilling this buffer on its own and
    // nothing has to re-arm it per frame.
    cam_ok = cameraStart((uint8_t *)cam_buf, CAMERA_MODE_CONTINUOUS);
  }
}

void loop()
{
  if (board.lcd.available())
  {
    if (cam_ok) {
      resize_image_t src = { HW_CAMERA_WIDTH, HW_CAMERA_HEIGHT, 0, 0,
                             HW_CAMERA_WIDTH, cam_buf };
      resize_image_t dst = { board.lcd.width(), board.lcd.height(), 0, 0,
                             board.lcd.width(), board.lcd.frameBuffer() };
      resizeImageFast(&src, &dst);
    } else {
      board.lcd.clear(black);
      board.lcd.printf(4, 2, red, "카메라 없음");
    }
    board.lcd.update();
  }

  board.ledToggle();
  delay(30);
}
