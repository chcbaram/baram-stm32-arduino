#include "SD.h"

extern "C" {
}

SDClass SD;

// FatFs open modes, from the Arduino ones.
static BYTE toFatfsMode(uint8_t mode)
{
  BYTE m = 0;

  if (mode & O_READ)  m |= FA_READ;
  if (mode & O_WRITE) m |= FA_WRITE;

  // FatFs spells the creation policy as one of three flags rather than a set of
  // independent bits, so the combination has to be decided here. Most specific
  // first: O_EXCL means fail if it is already there, O_TRUNC means start empty.
  if ((mode & O_CREAT) && (mode & O_EXCL)) m |= FA_CREATE_NEW;
  else if (mode & O_TRUNC)                 m |= FA_CREATE_ALWAYS;
  else if (mode & O_CREAT)                 m |= FA_OPEN_ALWAYS;

  // O_SYNC has no FatFs equivalent - it syncs on close and on f_sync(), which
  // File::flush() calls - so it is accepted and has no effect.

  if (m == 0) m = FA_READ;
  return m;
}

bool SDClass::begin()
{
  if (_mounted) return true;

  // Ask the socket first, so an empty slot is reported as such rather than as
  // a card that would not initialise.
  if (!sdIsDetected()) return false;
  if (!sdInit()) return false;

  // Mount now rather than on first access: that is what lets begin() answer
  // truthfully about whether there is a filesystem to talk to. The empty volume
  // string is drive 0, the only one - FF_VOLUMES is 1.
  if (f_mount(&_fs, "", 1) != FR_OK) return false;

  _mounted = true;
  return true;
}

void SDClass::end()
{
  if (!_mounted) return;
  f_mount(nullptr, "", 0);
  sdDeInit();
  _mounted = false;
}

File SDClass::open(const char *path, uint8_t mode)
{
  File f;

  if (!_mounted || path == nullptr) return f;

  strncpy(f._path, path, sizeof(f._path) - 1);
  // The name is the last component, which is what Arduino's File::name()
  // reports.
  const char *slash = strrchr(path, '/');
  strncpy(f._name, slash ? slash + 1 : path, sizeof(f._name) - 1);

  FILINFO info;
  bool is_root = (strcmp(path, "/") == 0 || path[0] == '\0');

  if (is_root || (f_stat(path, &info) == FR_OK && (info.fattrib & AM_DIR))) {
    if (f_opendir(&f._dir, path) != FR_OK) return f;
    f._isdir = true;
    f._open  = true;
    if (is_root) strncpy(f._name, "/", sizeof(f._name) - 1);
    return f;
  }

  if (f_open(&f._fil, path, toFatfsMode(mode)) != FR_OK) return f;

  // FILE_WRITE appends, matching the Arduino library.
  if ((mode & O_APPEND) && (mode & O_WRITE)) {
    f_lseek(&f._fil, f_size(&f._fil));
  }
  f._open = true;
  return f;
}

bool SDClass::exists(const char *path)
{
  if (!_mounted) return false;
  FILINFO info;
  return f_stat(path, &info) == FR_OK;
}

bool SDClass::mkdir(const char *path)
{
  if (!_mounted || path == nullptr) return false;

  // The standard library creates the intermediate directories too, so
  // mkdir("/data/logs/2026") works in one call. f_mkdir only makes the last
  // component, so walk the path and make each one in turn.
  char work[256];
  strncpy(work, path, sizeof(work) - 1);
  work[sizeof(work) - 1] = '\0';

  for (char *p = work + 1; *p != '\0'; p++) {
    if (*p != '/') continue;
    *p = '\0';
    FRESULT r = f_mkdir(work);
    if (r != FR_OK && r != FR_EXIST) return false;
    *p = '/';
  }

  FRESULT r = f_mkdir(work);
  return r == FR_OK || r == FR_EXIST;
}

bool SDClass::remove(const char *path)
{
  if (!_mounted) return false;
  return f_unlink(path) == FR_OK;
}

bool SDClass::rmdir(const char *path)
{
  // f_unlink removes an empty directory too.
  return remove(path);
}

uint64_t SDClass::cardSize()
{
  sd_info_t info;
  if (!sdGetInfo(&info)) return 0;
  return (uint64_t)info.log_block_numbers * info.log_block_size;
}

uint64_t SDClass::totalBytes()
{
  if (!_mounted) return 0;
  FATFS *fs;
  DWORD free_clusters;
  if (f_getfree("", &free_clusters, &fs) != FR_OK) return 0;
  return (uint64_t)(fs->n_fatent - 2) * fs->csize * FF_MAX_SS;
}

uint64_t SDClass::usedBytes()
{
  if (!_mounted) return 0;
  FATFS *fs;
  DWORD free_clusters;
  if (f_getfree("", &free_clusters, &fs) != FR_OK) return 0;
  uint64_t total = (uint64_t)(fs->n_fatent - 2) * fs->csize * FF_MAX_SS;
  uint64_t freed = (uint64_t)free_clusters * fs->csize * FF_MAX_SS;
  return total - freed;
}

// ---------------------------------------------------------------- File -----

int File::available()
{
  if (!_open || _isdir) return 0;
  int n = (int)(f_size(&_fil) - f_tell(&_fil));
  if (_peeked >= 0) n++;
  return n;
}

int File::read()
{
  if (_peeked >= 0) {
    int c = _peeked;
    _peeked = -1;
    return c;
  }
  if (!_open || _isdir) return -1;

  uint8_t b;
  UINT got = 0;
  if (f_read(&_fil, &b, 1, &got) != FR_OK || got == 0) return -1;
  return b;
}

int File::peek()
{
  if (_peeked < 0) _peeked = read();
  return _peeked;
}

int File::read(void *buf, size_t size)
{
  if (!_open || _isdir || buf == nullptr) return -1;

  uint8_t *p = (uint8_t *)buf;
  size_t   n = 0;

  if (_peeked >= 0 && size > 0) {
    *p++ = (uint8_t)_peeked;
    _peeked = -1;
    n = 1;
    size--;
  }
  if (size > 0) {
    UINT got = 0;
    if (f_read(&_fil, p, size, &got) != FR_OK) return n > 0 ? (int)n : -1;
    n += got;
  }
  return (int)n;
}

size_t File::write(uint8_t b)
{
  return write(&b, 1);
}

size_t File::write(const uint8_t *buf, size_t size)
{
  if (!_open || _isdir) return 0;
  UINT written = 0;
  if (f_write(&_fil, buf, size, &written) != FR_OK) return 0;
  return written;
}

void File::flush()
{
  if (_open && !_isdir) f_sync(&_fil);
}

bool File::seek(uint32_t pos)
{
  if (!_open || _isdir) return false;
  _peeked = -1;
  return f_lseek(&_fil, pos) == FR_OK;
}

uint32_t File::position()
{
  if (!_open || _isdir) return 0;
  return f_tell(&_fil);
}

uint32_t File::size()
{
  if (!_open || _isdir) return 0;
  return f_size(&_fil);
}

void File::close()
{
  if (!_open) return;
  if (_isdir) f_closedir(&_dir);
  else        f_close(&_fil);
  _open = false;
}

bool File::isDirectory()
{
  return _open && _isdir;
}

void File::rewindDirectory()
{
  if (_open && _isdir) f_readdir(&_dir, nullptr);
}

File File::openNextFile(uint8_t mode)
{
  File next;
  if (!_open || !_isdir) return next;

  FILINFO info;
  // An empty name means the end of the directory, which is what closes the
  // usual `while (File e = dir.openNextFile())` loop.
  if (f_readdir(&_dir, &info) != FR_OK || info.fname[0] == '\0') return next;

  char path[sizeof(_path)];
  bool at_root = (_path[0] == '\0' || strcmp(_path, "/") == 0);
  snprintf(path, sizeof(path), "%s%s%s", at_root ? "/" : _path,
           at_root ? "" : "/", info.fname);
  // FatFs hands back UTF-8 now, so a name with Hangul in it costs three bytes
  // per character. snprintf truncates rather than overruns; _path is 256, which
  // is about 85 characters.

  return SD.open(path, mode);
}
