/*
 * sd_sdmmc.c
 *
 * The SDMMC back end. This is the part that knows about the peripheral, its
 * DMA, cache lines and card states; which block and which pins it drives come
 * from hw_def.h.
 *
 * sd.c owns the interface a board-independent caller sees and dispatches into
 * the table this file fills in - the same shape as lcd.c and lcd/st7735.c.
 */

#include "hw/driver/sd/sd_sdmmc.h"

#ifdef _USE_HW_SDMMC

#include <string.h>

/*
 * Which parts this back end is written for.
 *
 * The HAL_SD_* names are the same across every STM32 family, but two things
 * underneath are not, and both of them are in this file:
 *
 *   - The DMA model. H7, H5, L4 and U5 have the DMA inside the SDMMC block, so
 *     HAL_SD_ReadBlocks_DMA() needs nothing linked to it. F4 and F7 drive an
 *     external stream and want __HAL_LINKDMA() in MspInit plus DMA IRQ handlers.
 *   - Cache maintenance. SCB_*DCache_by_Addr() is Cortex-M7. Parts without a
 *     data cache do not have it, and H5's M33 manages its caches through
 *     separate ICACHE/DCACHE peripherals.
 *
 * So a new family gets its own back end beside this one rather than #ifdef
 * blocks in here - sd.c dispatches through a table for exactly that reason, and
 * nothing above this file has to change. Stopping here with a message beats
 * fifty errors from code that was never meant to build.
 */
#if !defined(STM32H7xx)
#error "sd_sdmmc.c is written for STM32H7. Add a back end for this family beside it and point _USE_HW_* at it."
#endif


#define CACHE_LINE          32
// (was used to pick a direct DMA path into the caller's buffer; see the note
// above sd_bounce for why that path is gone)

#ifndef HW_SD_BOUNCE_SECTORS
#define HW_SD_BOUNCE_SECTORS  8
#endif


//-- Internal Variables
//
static bool is_init = false;
static volatile bool is_rx_done = false;
static volatile bool is_tx_done = false;
static volatile bool is_error   = false;

// Aligned so the handle never shares a cache line with whatever precedes it in
// .bss. Nothing widens a cache range any more, but this handle sat directly
// behind FatFs's sector window and a single line-rounded invalidate wiped its
// Instance field. Cheap insurance against that ever coming back.
static SD_HandleTypeDef uSdHandle __attribute__((aligned(32)));

static uint8_t sd_bounce[HW_SD_BOUNCE_SECTORS * BLOCKSIZE] __attribute__((aligned(CACHE_LINE)));


//-- Internal Functions
//
static void cacheClean(const void *addr, uint32_t size);
static void cacheInvalidate(const void *addr, uint32_t size);
static bool sdWaitDone(volatile bool *p_done, uint32_t timeout_ms);
static bool sdFail(void);
static bool sdReadDirect(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
static bool sdWriteDirect(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);


//-- Initialisation
//
bool sdmmcInit(void)
{
  HAL_StatusTypeDef status;


  /* uSD device interface configuration */
  uSdHandle.Instance = HW_SD_INSTANCE;

  uSdHandle.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  uSdHandle.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  uSdHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  uSdHandle.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  uSdHandle.Init.ClockDiv            = HW_SD_CLK_DIV;



  if (sdmmcIsDetected() != true)
  {
    logPrintf("sdCard     \t\t: not connected\r\n");
    return false;
  }
  else
  {
    logPrintf("sdCard     \t\t: connected\r\n");
  }


  HAL_SD_DeInit(&uSdHandle);
  status = HAL_SD_Init(&uSdHandle);
  if(status != HAL_OK)
  {
    logPrintf("sdCard     \t\t: fail, %d\r\n", status);
    return false;
  }
  else
  {
    logPrintf("sdCard     \t\t: OK\r\n");
  }

  is_init = true;



  return is_init;
}

bool sdmmcDeInit(void)
{
  bool ret = true;


  uSdHandle.Instance = HW_SD_INSTANCE;

  if(HAL_SD_DeInit(&uSdHandle) != HAL_OK)
  {
    ret = false;
  }

  // HAL_SD_DeInit calls MspDeInit, which already released the clock, the pins
  // and the interrupt.
  is_init = false;

  return ret;
}


// Hands sd.c the entry points it dispatches through.
bool sdmmcInitDriver(sd_driver_t *p_driver)
{
  p_driver->init         = sdmmcInit;
  p_driver->deinit       = sdmmcDeInit;
  p_driver->isDetected   = sdmmcIsDetected;
  p_driver->isBusy       = sdmmcIsBusy;
  p_driver->read         = sdmmcRead;
  p_driver->write        = sdmmcWrite;
  p_driver->erase        = sdmmcErase;
  p_driver->getInfo      = sdmmcGetInfo;
  p_driver->getLastError = sdmmcGetLastError;
  return true;
}


//-- External Functions
//
bool sdmmcRead(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (is_init == false) return false;

  // Every transfer goes through the bounce buffer. See the note above it for
  // why the direct path is gone.
  //
  // As many sectors per transfer as the bounce buffer holds.
  for (uint32_t done = 0; done < num_of_blocks; )
  {
    uint32_t n = num_of_blocks - done;
    if (n > HW_SD_BOUNCE_SECTORS) n = HW_SD_BOUNCE_SECTORS;

    if (sdReadDirect(block_addr + done, sd_bounce, n, timeout_ms) == false)
    {
      return false;
    }
    memcpy(&p_data[done * BLOCKSIZE], sd_bounce, n * BLOCKSIZE);
    done += n;
  }
  return true;
}

bool sdmmcWrite(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (is_init == false) return false;

  for (uint32_t done = 0; done < num_of_blocks; )
  {
    uint32_t n = num_of_blocks - done;
    if (n > HW_SD_BOUNCE_SECTORS) n = HW_SD_BOUNCE_SECTORS;

    memcpy(sd_bounce, &p_data[done * BLOCKSIZE], n * BLOCKSIZE);
    if (sdWriteDirect(block_addr + done, sd_bounce, n, timeout_ms) == false)
    {
      return false;
    }
    done += n;
  }
  return true;
}

bool sdmmcErase(uint32_t start_addr, uint32_t end_addr)
{
  bool ret = false;

  if (is_init == false) return false;

  if(HAL_SD_Erase(&uSdHandle, start_addr, end_addr) == HAL_OK)
  {
    ret = true;
  }

  return ret;
}

bool sdmmcIsBusy(void)
{
  bool is_busy;


  if (HAL_SD_GetCardState(&uSdHandle) == HAL_SD_CARD_TRANSFER )
  {
    is_busy = false;
  }
  else
  {
    is_busy = true;
  }

  return is_busy;
}

bool sdmmcIsDetected(void)
{
  bool ret = false;


#ifdef HW_SD_DETECT_NONE
  /* No detect line: a card is taken to be present, and sdmmcInit() failing is what
   * reports an empty slot. */
  ret = true;
#else
  static bool detect_ready = false;

  if (detect_ready == false)
  {
    GPIO_InitTypeDef gpio_init = {0};

    HW_SD_DETECT_CLK_ENABLE();
    gpio_init.Pin   = HW_SD_DETECT_PIN;
    gpio_init.Mode  = GPIO_MODE_INPUT;
    gpio_init.Pull  = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(HW_SD_DETECT_PORT, &gpio_init);
    detect_ready = true;
  }

  if (HAL_GPIO_ReadPin(HW_SD_DETECT_PORT, HW_SD_DETECT_PIN) == HW_SD_DETECT_PRESENT)
  {
    ret = true;
  }
#endif

  return ret;
}

bool sdmmcGetInfo(sd_info_t *p_info)
{
  bool ret = false;
  sd_info_t *p_sd_info = (sd_info_t *)p_info;

  HAL_SD_CardInfoTypeDef card_info;


  if (is_init == true)
  {
    HAL_SD_GetCardInfo(&uSdHandle, &card_info);

    p_sd_info->card_type          = card_info.CardType;
    p_sd_info->card_version       = card_info.CardVersion;
    p_sd_info->card_class         = card_info.Class;
    p_sd_info->rel_card_Add       = card_info.RelCardAdd;
    p_sd_info->block_numbers      = card_info.BlockNbr;
    p_sd_info->block_size         = card_info.BlockSize;
    p_sd_info->log_block_numbers  = card_info.LogBlockNbr;
    p_sd_info->log_block_size     = card_info.LogBlockSize;
    p_sd_info->card_size          =  (uint32_t)((uint64_t)p_sd_info->block_numbers * (uint64_t)p_sd_info->block_size / (uint64_t)1024 / (uint64_t)1024);
    ret = true;
  }

  return ret;
}

uint32_t sdmmcGetLastError(void)
{
  return uSdHandle.ErrorCode;
}


//-- Entry points the HAL and the vector table call
//
void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
  is_error = true;
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
  is_tx_done = true;
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
  is_rx_done = true;
}

void HW_SD_IRQHandler(void)
{
  HAL_SD_IRQHandler(&uSdHandle);
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
  GPIO_InitTypeDef gpio_init_structure;


  /* Which block and which pins comes from hw_def.h, so a board that wires the
   * socket differently does not need this file changed. */
  HW_SD_CLK_ENABLE();

  HW_SD_BUS_CLK_ENABLE();
  HW_SD_CMD_CLK_ENABLE();

  gpio_init_structure.Mode      = GPIO_MODE_AF_PP;
  gpio_init_structure.Pull      = GPIO_PULLUP;
  gpio_init_structure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_structure.Alternate = HW_SD_AF;

  gpio_init_structure.Pin = HW_SD_BUS_PINS;
  HAL_GPIO_Init(HW_SD_BUS_PORT, &gpio_init_structure);

  gpio_init_structure.Pin = HW_SD_CMD_PINS;
  HAL_GPIO_Init(HW_SD_CMD_PORT, &gpio_init_structure);

  HAL_NVIC_SetPriority(HW_SD_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(HW_SD_IRQn);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{
  if (sdHandle->Instance == HW_SD_INSTANCE)
  {
    HW_SD_CLK_DISABLE();

    // Same macros MspInit uses. Naming the pins here again is what makes a
    // second board release the wrong ones.
    HAL_GPIO_DeInit(HW_SD_BUS_PORT, HW_SD_BUS_PINS);
    HAL_GPIO_DeInit(HW_SD_CMD_PORT, HW_SD_CMD_PINS);

    HAL_NVIC_DisableIRQ(HW_SD_IRQn);
  }
}


//-- Internal Functions
//
/*
 * Cache maintenance around DMA.
 *
 * The core turns the D-cache on and the SDMMC's own DMA writes straight to
 * memory, so a read has to be invalidated afterwards or the CPU keeps serving
 * stale lines.
 *
 * What the cache does NOT do here is lose a neighbour's data. An earlier
 * version of this comment claimed it did - that invalidating a line-rounded
 * range threw away dirty bytes belonging to whatever sat beside the buffer.
 * That was wrong. Read from the running target:
 *
 *   MPU_CTRL 0x00000005          enabled, background map for privileged
 *   R1 RBAR  0x24000001          AXI SRAM at 0x24000000
 *   R1 RASR  0x13020025          XN=1 AP=3 TEX=000 S=0 C=1 B=0, 512K
 *
 * TEX=000 C=1 B=0 is Normal, write-through. Every store reaches memory as it is
 * made, so a line in this region is never dirty and invalidating one can only
 * discard a clean copy. The bootloader sets this up and the Arduino core never
 * touches the MPU, so it is what every sketch runs under.
 *
 * That reasoning is sound and it is also not the whole story. Measured on this
 * board, a transfer whose target is a caller's buffer ON THE STACK faults even
 * when the buffer is cache line aligned and a whole number of lines long:
 *
 *   SdAlign, direct DMA into an alignas(32) File on the stack   39/40 fault
 *   SdAlign, every transfer through the bounce buffer below     40/40 clean
 *
 * Forty runs each, same binary re-run without rebuilding, bootloader fixed at
 * V260830R8. The fault is UNDEFINSTR with the stacked PC pointing at the second
 * halfword of a bl - control flow arrived somewhere that is not an instruction,
 * which is what a smashed return address looks like.
 *
 * Two things it is NOT. Swapping the invalidate for a clean+invalidate does not
 * help (37/40 still fault), so it is not a dirty line being discarded, which
 * matches the write-through mapping above. And the bounce buffer is ordinary
 * .bss in the same cacheable AXI SRAM, so it is not the memory attributes
 * either - the difference is only that it is not the stack.
 *
 * The mechanism is not understood. What is measured is that the direct path is
 * unsafe here, so there is no direct path: every transfer bounces. The cost is
 * a memcpy and a cap of eight sectors per transaction, which is worth paying
 * for a fault that corrupts the caller's return address.
 *
 * (A fourth build, direct path with the cache maintenance removed, came back
 * 40/40 clean and was a false negative - dumping the sketch's own log showed
 * SD.begin() had failed, so it never reached the code that faults. Any run of
 * this has to check that the card actually mounted before the count means
 * anything.)
 *
 * cacheClean() before a write stays. It is a no-op under write-through and is
 * correct if the mapping ever changes.
 */
/*
 * Cache maintenance around DMA.
 *
 * The core turns the D-cache on and the SDMMC's own DMA writes straight to
 * memory, so a read has to be invalidated afterwards or the CPU keeps serving
 * stale lines.
 *
 * What the cache does NOT do here is lose a neighbour's data. An earlier
 * version of this comment claimed it did - that invalidating a line-rounded
 * range threw away dirty bytes belonging to whatever sat beside the buffer.
 * That was wrong. Read from the running target:
 *
 *   MPU_CTRL 0x00000005          enabled, background map for privileged
 *   R1 RBAR  0x24000001          AXI SRAM at 0x24000000
 *   R1 RASR  0x13020025          XN=1 AP=3 TEX=000 S=0 C=1 B=0, 512K
 *
 * TEX=000 C=1 B=0 is Normal, write-through. Every store reaches memory as it is
 * made, so a line in this region is never dirty and invalidating one can only
 * discard a clean copy. The bootloader sets this up and the Arduino core never
 * touches the MPU, so it is what every sketch runs under.
 *
 * Two things follow. cacheClean() before a write is a no-op in practice, kept
 * because it is correct and costs nothing. And the bounce buffer below is not
 * load-bearing for safety here - it is insurance for the day this runs under a
 * write-back mapping, where a buffer sharing a line with anything else really
 * would corrupt it.
 *
 * The bounce buffer earns its keep for a different reason anyway: it holds
 * eight sectors, so a misaligned caller still gets multi-block transfers. One
 * sector per transaction measured ten times slower on writes.
 */
// Guarded on the core rather than the family: a part without a data cache has
// nothing to maintain, and the calls do not exist there to be compiled.
static void cacheClean(const void *addr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_CleanDCache_by_Addr((uint32_t *)addr, (int32_t)size);
#else
  (void)addr; (void)size;
#endif
}

static void cacheInvalidate(const void *addr, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_InvalidateDCache_by_Addr((uint32_t *)addr, (int32_t)size);
#else
  (void)addr; (void)size;
#endif
}

/*
 * Waits for a DMA completion flag, then for the card to leave its busy state.
 *
 * The error flag matters as much as the timeout. When a transfer fails the HAL
 * signals it through HAL_SD_ErrorCallback and never raises the completion flag,
 * so without watching for it this would sit out the whole timeout on every
 * failure - seconds at a time, per sector.
 */
static bool sdWaitDone(volatile bool *p_done, uint32_t timeout_ms)
{
  uint32_t pre_time = millis();

  while (*p_done == false)
  {
    if (is_error) return false;
    if (millis() - pre_time >= timeout_ms) return false;
  }

  // Fresh budget for the card's own busy period. Sharing one with the wait
  // above means a transfer that took most of the timeout leaves nothing here,
  // and a perfectly good write is reported as a failure and then aborted.
  pre_time = millis();
  while (sdmmcIsBusy() == true)
  {
    if (millis() - pre_time >= timeout_ms) return false;
  }
  return true;
}

/*
 * Gives up on a transfer that did not finish.
 *
 * This has to abort, not just return. The transfer is still armed, and the
 * caller is about to let go of the buffer - FatFs hands over its own window or
 * a file object that may live on the stack. A transfer left running writes into
 * memory that has since been reused, which shows up much later as a wild branch
 * with nothing to connect it back to the SD driver.
 */
static bool sdFail(void)
{
  HAL_SD_Abort(&uSdHandle);
  return false;
}

static bool sdReadDirect(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  uint32_t size = num_of_blocks * BLOCKSIZE;

  // Drop any dirty lines first. If one were evicted while the DMA is running it
  // would land on top of what the card just delivered.
  cacheInvalidate(p_data, size);

  is_rx_done = false;
  is_error   = false;

  if (HAL_SD_ReadBlocks_DMA(&uSdHandle, p_data, block_addr, num_of_blocks) != HAL_OK)
  {
    return false;      // nothing was started, so there is nothing to abort
  }
  if (sdWaitDone(&is_rx_done, timeout_ms) == false)
  {
    return sdFail();
  }

  cacheInvalidate(p_data, size);
  return true;
}

static bool sdWriteDirect(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  // Push the data out of the cache before the DMA reads it.
  cacheClean(p_data, num_of_blocks * BLOCKSIZE);

  is_tx_done = false;
  is_error   = false;

  if (HAL_SD_WriteBlocks_DMA(&uSdHandle, p_data, block_addr, num_of_blocks) != HAL_OK)
  {
    return false;
  }
  if (sdWaitDone(&is_tx_done, timeout_ms) == false)
  {
    return sdFail();
  }
  return true;
}

#endif /* _USE_HW_SDMMC */
