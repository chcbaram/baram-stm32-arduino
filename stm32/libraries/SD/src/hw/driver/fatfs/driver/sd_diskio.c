/*
 * sd_diskio.c
 *
 * The glue between FatFs and the card driver. FatFs asks for blocks by number;
 * hw/driver/sd.c dispatches that to whichever back end the board has.
 *
 * There is one volume, so pdrv is ignored rather than looked up in a table.
 * The generic-driver layer ST ships (ff_gen_drv.c) exists to register several
 * media at runtime; with a single socket it is indirection for its own sake,
 * and it was also where a stale registration could break a mount.
 */

#include "hw/driver/sd.h"

#include "lib/FatFs/source/ff.h"
#include "lib/FatFs/source/diskio.h"


#define SD_TIMEOUT_MS   5000


DSTATUS disk_initialize(BYTE pdrv)
{
  (void)pdrv;
  // sd.c brings the card up; by the time FatFs asks, SD.begin() has done it.
  return sdIsInit() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
  (void)pdrv;
  return sdIsInit() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  (void)pdrv;

  if (!sdIsInit()) return RES_NOTRDY;
  if (!sdReadBlocks((uint32_t)sector, (uint8_t *)buff, count, SD_TIMEOUT_MS)) return RES_ERROR;
  return RES_OK;
}

#if !FF_FS_READONLY
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  (void)pdrv;

  if (!sdIsInit()) return RES_NOTRDY;
  if (!sdWriteBlocks((uint32_t)sector, (uint8_t *)buff, count, SD_TIMEOUT_MS)) return RES_ERROR;
  return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  sd_info_t info;

  (void)pdrv;

  if (!sdIsInit()) return RES_NOTRDY;

  switch (cmd)
  {
    case CTRL_SYNC:
      // Writes are complete when sdWriteBlocks() returns - it waits for the
      // card to leave its busy state - so there is nothing buffered here.
      return RES_OK;

    case GET_SECTOR_COUNT:
      if (!sdGetInfo(&info)) return RES_ERROR;
      *(LBA_t *)buff = info.log_block_numbers;
      return RES_OK;

    case GET_SECTOR_SIZE:
      if (!sdGetInfo(&info)) return RES_ERROR;
      *(WORD *)buff = (WORD)info.log_block_size;
      return RES_OK;

    case GET_BLOCK_SIZE:
      // In sectors, for the allocation unit. One is always correct and only
      // costs a little erase efficiency on f_mkfs, which is not built.
      *(DWORD *)buff = 1;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
