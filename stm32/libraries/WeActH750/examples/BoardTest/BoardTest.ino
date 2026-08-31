/*
 * WEACT-H750-MINI - everything on the board, from one object.
 *
 * No external libraries: the LCD driver, the fonts and the Hangul composer all
 * ship with the board package.
 *
 * Hold K1 for a second to hand the board back to the bootloader, which is the
 * same thing the IDE does for you before an upload.
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

  Serial.println(board.version());
  Serial.print("LCD ");
  Serial.print(board.lcd.width());
  Serial.print("x");
  Serial.println(board.lcd.height());
}

void loop()
{
  static uint32_t count = 0;
  static uint32_t held  = 0;

  if (board.lcd.available())
  {
    board.lcd.clear(black);
    board.lcd.rect(0, 0, board.lcd.width(), board.lcd.height(), darkgray);

    board.lcd.setFont(LCD_FONT_HAN);
    board.lcd.printf(6, 4, white, "WEACT-H750-MINI");
    board.lcd.printf(6, 22, yellow, "카운트 %lu", count);

    board.lcd.fillCircle(142, 34, 10, board.keyPressed() ? green : red);
    board.lcd.printf(6, 40, lightblue, "K1 %s", board.keyPressed() ? "눌림" : "안눌림");
    board.lcd.printf(6, 58, gray, "%lu ms/frame", board.lcd.drawTime());

    board.lcd.update();
  }

  board.ledToggle();

  if (board.keyPressed())
  {
    if (++held > 20)          // about a second at 50 ms a pass
    {
      board.lcd.clear(black);
      board.lcd.printf(24, 32, white, "부트로더로");
      board.lcd.update();
      delay(500);
      board.enterBootloader();
    }
  }
  else
  {
    held = 0;
  }

  count++;
  delay(50);
}
