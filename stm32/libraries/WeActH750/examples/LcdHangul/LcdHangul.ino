/*
 * WEACT-H750-MINI - Korean text on the LCD.
 *
 * Strings are UTF-8. Hangul syllables are composed at draw time from initial,
 * medial and final jamo rather than stored one bitmap per syllable - there are
 * 11,172 of them - which is what keeps the whole language inside about 80 KB.
 *
 * printfResize() draws the same font at any height. Its fourth argument is that
 * height in pixels, not a multiplier: the base font is 16 tall, so 32 is double
 * size. The ceiling is 64.
 */

#include <WeActH750.h>

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);
}

void loop()
{
  static const char *words[] = { "바람", "안녕하세요", "한글 출력", "테스트" };
  static uint8_t index = 0;

  if (board.lcd.available())
  {
    board.lcd.clear(black);

    board.lcd.printf(4, 4, white, "%s", words[index]);
    board.lcd.printf(4, 24, yellow, "WEACT-H750 %d", index);

    // 32 pixels tall - double the 16 pixel base - from the same font.
    board.lcd.printfResize(4, 44, lightblue, 32.0f, "%s", words[index]);

    board.lcd.update();
    index = (index + 1) % (sizeof(words) / sizeof(words[0]));
  }

  delay(1000);
}
