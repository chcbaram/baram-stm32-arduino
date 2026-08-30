/*
 * sd.c
 *
 * Dispatches to whichever back end the board provides. Everything that knows
 * about a peripheral lives under sd/; this file only knows that a block device
 * has blocks.
 *
 * The table is left empty on a board that names no back end, and every entry
 * point here answers false. That is deliberate: the library is part of the
 * platform, so it is compiled for every board in the package, and a board
 * without a card socket must still build.
 */

#include "hw/driver/sd.h"
#include "hw/driver/sd/sd_sdmmc.h"


//-- Internal Variables
//
static sd_driver_t sd = {0};
static bool is_init   = false;


//-- Initialisation
//

// Picks the back end the board asked for. A second one - an SPI socket, say -
// is another file under sd/ and another line here; nothing else in the library
// changes, because nothing else in the library names a bus.
//
//   #ifdef _USE_HW_SD_SPI
//     sdSpiInitDriver(&sd);
//   #endif
static void sdSelectDriver(void)
{
#ifdef _USE_HW_SDMMC
  sdmmcInitDriver(&sd);
#endif
}

bool sdInit(void)
{
  is_init = false;

  sdSelectDriver();

  // No back end on this board. Not an error - just nothing to talk to.
  if (sd.init == NULL) return false;

  is_init = sd.init();
  return is_init;
}

bool sdDeInit(void)
{
  is_init = false;

  if (sd.deinit == NULL) return true;
  return sd.deinit();
}


//-- External Functions
//

bool sdIsInit(void)
{
  return is_init;
}

bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (sd.read == NULL) return false;
  return sd.read(block_addr, p_data, num_of_blocks, timeout_ms);
}

bool sdWriteBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (sd.write == NULL) return false;
  return sd.write(block_addr, p_data, num_of_blocks, timeout_ms);
}

bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr)
{
  if (sd.erase == NULL) return false;
  return sd.erase(start_addr, end_addr);
}

bool sdIsBusy(void)
{
  if (sd.isBusy == NULL) return false;
  return sd.isBusy();
}

bool sdIsDetected(void)
{
  // Asked before sdInit() by callers that want to know whether to bother, so
  // the table has to be filled in here too.
  if (sd.isDetected == NULL) sdSelectDriver();

  if (sd.isDetected == NULL) return false;
  return sd.isDetected();
}

bool sdGetInfo(sd_info_t *p_info)
{
  if (sd.getInfo == NULL) return false;
  return sd.getInfo(p_info);
}

uint32_t sdGetLastError(void)
{
  if (sd.getLastError == NULL) return 0;
  return sd.getLastError();
}
