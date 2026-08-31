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

  /*
   * Wait for the host to open the port, at most two seconds.
   *
   * Serial here is USB CDC, not a UART. Nothing is buffered for a port that is
   * not open yet, so anything printed before the host gets round to opening it
   * is simply gone - and after an upload the board re-enumerates under a
   * different USB id, which takes the host a moment.
   *
   * `while (!Serial)` is the idiom every native-USB Arduino board uses, and
   * operator bool() reports whether the host has raised DTR. The timeout is
   * there because a board running on its own never sees a port opened at all,
   * and without it the sketch would wait for ever. A plain delay() would work
   * but always costs the full wait, even when a terminal is already attached.
   */
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000; ) {
  }
  board.lcd.setFont(LCD_FONT_HAN);

  if (board.cam.begin(FRAMESIZE_QQVGA)) {
    Serial.printf("camera : %ld x %ld\n",
                  (long)board.cam.width(), (long)board.cam.height());
  } else {
    Serial.println(F("camera : not found"));
  }

  /*
   * Orientation is left alone here: the module on this board already faces the
   * same way as the panel, so what it captures is what someone looking at the
   * screen expects. board.cam.mirror() and board.cam.flip() are there for a
   * module mounted the other way round, and both are done in the sensor - a
   * reversed read on every row would cost far more.
   */
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

  /*
   * The LED is a heartbeat, not a frame counter.
   *
   * Toggling it once per pass blinked it at the frame rate, which reads as
   * permanently half-lit. Half a second is slow enough to see, and driving it
   * from millis() rather than from a delay keeps the loop free to draw as fast
   * as the panel allows.
   */
  static uint32_t led_time = 0;
  if (millis() - led_time >= 500) {
    led_time = millis();
    board.ledToggle();
  }
}
