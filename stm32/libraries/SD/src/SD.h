#pragma once

/*
 * Arduino's SD API, on this board's SDMMC socket.
 *
 * The names and behaviour match the standard SD library, so sketches and
 * libraries written against it work unchanged - File derives from Stream, which
 * is what lets anything taking a Stream& read from a card.
 *
 * It has to be a separate implementation because the standard library talks SPI
 * and this socket is wired to SDMMC1 in 4-bit mode, which is both faster and
 * the only thing the pins can do. Underneath is FatFs (ChaN's, permissively
 * licensed) over a HAL SDMMC driver, rather than STM32duino's STM32SD, which is
 * GPLv3 and would carry that licence into every sketch that opened a file.
 *
 *   #include <SD.h>
 *
 *   void setup() {
 *     Serial.begin(115200);
 *     if (!SD.begin()) { Serial.println("no card"); return; }
 *
 *     File f = SD.open("/log.txt", FILE_WRITE);
 *     f.println("hello");
 *     f.close();
 *
 *     File dir = SD.open("/");
 *     while (File e = dir.openNextFile()) {
 *       Serial.println(e.name());
 *       e.close();
 *     }
 *   }
 */

#include <Arduino.h>

extern "C" {
#include "hw/driver/sd.h"
#include "lib/FatFs/src/ff.h"
}

#define FILE_READ   O_READ
#define FILE_WRITE  (O_READ | O_WRITE | O_CREAT | O_APPEND)

enum {
  O_READ   = 0x01,
  O_WRITE  = 0x02,
  O_CREAT  = 0x10,
  O_APPEND = 0x20,
  O_TRUNC  = 0x40,
};

class File : public Stream
{
public:
  File() {}
  ~File() {}

  // Stream
  int    available() override;
  int    read() override;
  int    peek() override;
  void   flush() override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t *buf, size_t size) override;
  using Print::write;

  int    read(void *buf, size_t size);
  bool   seek(uint32_t pos);
  uint32_t position();
  uint32_t size();
  void   close();

  const char *name() const { return _name; }
  bool   isDirectory();

  // Directories only. Returns a closed File when there is nothing left, so
  // `while (File e = dir.openNextFile())` terminates.
  File   openNextFile(uint8_t mode = FILE_READ);
  void   rewindDirectory();

  operator bool() const { return _open; }

private:
  friend class SDClass;

  FIL   _fil = {};
  DIR   _dir = {};
  char  _name[256] = {0};
  char  _path[256] = {0};
  bool  _open = false;
  bool  _isdir = false;
  int   _peeked = -1;
};

class SDClass
{
public:
  // Brings up SDMMC and mounts the first FAT volume. False means no card, an
  // unreadable card, or no filesystem on it.
  bool begin();
  void end();

  File open(const char *path, uint8_t mode = FILE_READ);
  File open(const String &path, uint8_t mode = FILE_READ) { return open(path.c_str(), mode); }

  bool exists(const char *path);
  bool exists(const String &path) { return exists(path.c_str()); }
  bool mkdir(const char *path);
  bool mkdir(const String &path) { return mkdir(path.c_str()); }
  bool remove(const char *path);
  bool remove(const String &path) { return remove(path.c_str()); }
  bool rmdir(const char *path);
  bool rmdir(const String &path) { return rmdir(path.c_str()); }

  // Card size in bytes, or 0 if there is no card.
  uint64_t cardSize();
  // Free space on the mounted volume, in bytes.
  uint64_t totalBytes();
  uint64_t usedBytes();

private:
  bool  _mounted = false;
  FATFS _fs = {};
  char  _path[4] = {0};   // the volume FATFS_LinkDriver hands back
};

extern SDClass SD;
