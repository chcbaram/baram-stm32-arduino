/*
 * WEACT-H750-MINI - microSD, on screen.
 *
 * The card is on SDMMC1 in 4-bit mode, driven over DMA. SD.h presents the same
 * API as the standard Arduino SD library, so sketches and libraries written
 * against that work here even though the wiring is not SPI.
 *
 * <SD.h> has to be included by the sketch: arduino-cli finds libraries by
 * scanning the sketch's own includes, so one reached only through WeActH750.h
 * would be missed.
 */

#include <WeActH750.h>
#include <SD.h>

static bool  mounted  = false;
static char  status[64];
static char  entries[4][32];
static int   entry_count = 0;

void setup()
{
  board.begin(115200);
  board.lcd.setFont(LCD_FONT_HAN);

  mounted = board.sd.begin();

  if (!mounted) {
    snprintf(status, sizeof(status), "카드 없음");
  } else {
    uint32_t mb = (uint32_t)(board.sd.cardSize() / (1024 * 1024));
    snprintf(status, sizeof(status), "%lu MB", (unsigned long)mb);

    // Write a line, then read the root back so both directions are exercised.
    File f = board.sd.open("/weact.txt", FILE_WRITE);
    if (f) {
      f.print("hello from ");
      f.println(board.version());
      f.close();
    }

    File dir = board.sd.open("/");
    while (File e = dir.openNextFile()) {
      if (entry_count < 4) {
        snprintf(entries[entry_count], sizeof(entries[0]), "%s", e.name());
        entry_count++;
      }
      e.close();
    }
    dir.close();
  }
}

void loop()
{
  if (board.lcd.available())
  {
    board.lcd.clear(black);
    board.lcd.printf(4, 2, white, "microSD %s", status);

    for (int i = 0; i < entry_count; i++) {
      board.lcd.printf(4, 20 + i * 14, mounted ? lightgreen : gray, "%s", entries[i]);
    }
    if (mounted && entry_count == 0) {
      board.lcd.printf(4, 20, yellow, "빈 카드");
    }

    board.lcd.update();
  }

  board.ledToggle();
  delay(250);
}
