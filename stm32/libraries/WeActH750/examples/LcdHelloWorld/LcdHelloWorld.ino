/*
 * WEACT-H750-MINI - LCD basics.
 *
 * Drawing goes into a framebuffer; update() pushes it to the 160x80 ST7735S
 * panel. The driver is the bootloader's, so this looks exactly like its splash
 * screen.
 */

#include <WeActH750.h>

void setup()
{
  board.begin(115200);
}

void loop()
{
  static uint32_t frames = 0;

  // False while the previous frame is still going out.
  if (board.lcd.available())
  {
    board.lcd.clear(black);
    board.lcd.rect(0, 0, board.lcd.width(), board.lcd.height(), white);

    board.lcd.setFont(LCD_FONT_HAN);
    board.lcd.printf(6, 6, white, "WEACT-H750-MINI");

    board.lcd.fillRect(6, 26, 60, 20, blue);
    board.lcd.printf(12, 30, white, "%lu", frames);

    board.lcd.fillCircle(120, 40, 16, red);
    board.lcd.line(6, 56, 154, 56, gray);
    board.lcd.printf(6, 60, green, "%lu ms/frame", board.lcd.drawTime());

    board.lcd.update();
    frames++;
  }

  delay(30);
}
