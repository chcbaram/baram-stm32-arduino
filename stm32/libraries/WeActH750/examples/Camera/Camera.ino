/*
 * WEACT-H750-MINI - the DVP camera, on screen.
 *
 * The board's camera header carries an OV2640 on DCMI in 8 bit mode. The sensor
 * fills a frame buffer over DMA on its own, and the sketch copies the middle of
 * each frame onto the 160x80 panel.
 *
 * Everything the camera needs - the frame buffer's placement, the clock the
 * sensor runs from, the capture - belongs to board.cam, so a sketch says what
 * it wants rather than how to get it:
 *
 *   board.cam.begin()          find the sensor and start capturing
 *   board.cam.drawTo(lcd)      put the latest frame on the panel
 *   board.cam.mirror/flip      orientation, done in the sensor
 *   board.cam.colorBar(true)   the sensor's test pattern, for bring-up
 */

#include <WeActH750.h>

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  if (board.cam.begin(FRAMESIZE_QQVGA)) {
    Serial.printf("camera : %ld x %ld\n",
                  (long)board.cam.width(), (long)board.cam.height());
  } else {
    Serial.println(F("camera : not found"));
  }

  /*
   * The module sits above the panel and points the same way the panel faces, so
   * what it sees is mirrored relative to what someone looking at the screen
   * expects. Mirroring in the sensor costs nothing; doing it in the copy would
   * cost a reversed read on every row.
   */
  board.cam.mirror(true);
}

void loop()
{
  if (board.lcd.available())
  {
    if (board.cam.isRunning()) {
      board.cam.drawTo(board.lcd);

      /*
       * Overlay, drawn after the frame so it sits on top of it.
       *
       * The small ASCII font rather than the Hangul one: at 16 pixels tall the
       * latter would take a fifth of an 80 pixel panel. The frame rate comes
       * from the capture interrupt, so it reports what the sensor delivers -
       * the panel is slower, and its own rate would not tell you whether the
       * capture is healthy.
       */
      board.lcd.setFont(LCD_FONT_07x10);
      board.lcd.printf(2, 2, white, "%ldx%ld", (long)board.cam.width(),
                                               (long)board.cam.height());
      board.lcd.printf(2, board.lcd.height() - 12, white, "%lu fps",
                       (unsigned long)board.cam.fps());
    } else {
      board.lcd.setFont(LCD_FONT_HAN);
      board.lcd.clear(black);
      board.lcd.printf(4, 2,  red,  "카메라 없음");
      board.lcd.printf(4, 24, gray, "헤더 연결 확인");
    }
    board.lcd.update();
  }

  board.ledToggle();
  delay(10);
}
