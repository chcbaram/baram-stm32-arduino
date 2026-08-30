/*
 * sd.c
 *
 * microSD over SDMMC, 4 bit wide, through the SDMMC block's own DMA.
 *
 * Which block and which pins come from hw_def.h, so a board that wires the
 * socket differently only edits that file.
 *
 * Functions are laid out init first, then the interface, then what is only used
 * in here.
 */

#include "hw/driver/sd.h"


#ifdef _USE_HW_SD
/* Card detect would come from the board's GPIO layer. This board has no detect
 * line - HW_SD_DETECT_NONE in hw_def.h - so nothing is needed here. A board
 * that has one includes its own GPIO header. */

#define CACHE_LINE          32
#define IS_LINE_ALIGNED(a)  ((((uint32_t)(a)) & (CACHE_LINE - 1)) == 0)

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

#if HW_SD_USE_CMDIF == 1
void sdCmdifInit(void);
void sdCmdif(void);
#endif



//-- Initialisation
//

bool sdInit(void)
{
  HAL_StatusTypeDef status;


  /* uSD device interface configuration */
  uSdHandle.Instance = HW_SD_INSTANCE;

  uSdHandle.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  uSdHandle.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  uSdHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  uSdHandle.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  uSdHandle.Init.ClockDiv            = HW_SD_CLK_DIV;



  if (sdIsDetected() != true)
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


#if HW_SD_USE_CMDIF == 1
  static bool is_cmd_init = false;

  if (is_cmd_init == false)
  {
    sdCmdifInit();
    is_cmd_init = true;
  }
#endif

  return is_init;
}

bool sdDeInit(void)
{
  bool ret = true;


  uSdHandle.Instance = HW_SD_INSTANCE;

  if(HAL_SD_DeInit(&uSdHandle) != HAL_OK)
  {
    ret = false;
  }

  HAL_NVIC_DisableIRQ(HW_SD_IRQn);
  HW_SD_CLK_DISABLE();

  is_init = false;

  return ret;
}


//-- External Functions
//

bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (is_init == false) return false;

  if (IS_LINE_ALIGNED(p_data))
  {
    return sdReadDirect(block_addr, p_data, num_of_blocks, timeout_ms);
  }

  // Misaligned caller buffer: through the bounce buffer, as many sectors per
  // transfer as it holds.
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

bool sdWriteBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  if (is_init == false) return false;

  if (IS_LINE_ALIGNED(p_data))
  {
    return sdWriteDirect(block_addr, p_data, num_of_blocks, timeout_ms);
  }

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

bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr)
{
  bool ret = false;


  if(HAL_SD_Erase(&uSdHandle, start_addr, end_addr) == HAL_OK)
  {
    ret = true;
  }

  return ret;
}

bool sdIsBusy(void)
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

bool sdIsDetected(void)
{
  bool ret = false;


#ifdef HW_SD_DETECT_NONE
  /* No detect line: a card is taken to be present, and sdInit() failing is what
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

bool sdGetInfo(sd_info_t *p_info)
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

uint32_t sdGetLastError(void)
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

  HW_SD_DATA_CLK_ENABLE();
  HW_SD_CMD_CLK_ENABLE();

  gpio_init_structure.Mode      = GPIO_MODE_AF_PP;
  gpio_init_structure.Pull      = GPIO_PULLUP;
  gpio_init_structure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_structure.Alternate = HW_SD_AF;

  gpio_init_structure.Pin = HW_SD_DATA_PINS;
  HAL_GPIO_Init(HW_SD_DATA_PORT, &gpio_init_structure);

  gpio_init_structure.Pin = HW_SD_CMD_PINS;
  HAL_GPIO_Init(HW_SD_CMD_PORT, &gpio_init_structure);

  HAL_NVIC_SetPriority(HW_SD_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(HW_SD_IRQn);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{

  if (sdHandle->Instance == HW_SD_INSTANCE)
  {
    /* Peripheral clock disable */
    HW_SD_CLK_DISABLE();

    /**SDMMC1 GPIO Configuration
    PC10     ------> SDMMC1_D2
    PC11     ------> SDMMC1_D3
    PC12     ------> SDMMC1_CK
    PD2     ------> SDMMC1_CMD
    PC8     ------> SDMMC1_D0
    PC9     ------> SDMMC1_D1
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_8
                          |GPIO_PIN_9);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);

    /* SDMMC1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(HW_SD_IRQn);
  }
}


//-- Internal Functions
//

/*
 * Cache maintenance around DMA.
 *
 * The core turns the D-cache on, and the SDMMC's internal DMA writes straight
 * to memory. Without invalidating after a read the CPU keeps serving stale
 * cache lines; without cleaning before a write the card gets stale memory.
 *
 * The unit is a whole 32-byte line and there is no way around that. CMSIS
 * rounds to lines itself - SCB_InvalidateDCache_by_Addr adds the misalignment
 * to the length and walks from the unaligned address, so DCIMVAC lands on the
 * line containing it either way. Passing an exact range buys nothing.
 *
 * That matters because invalidating discards a dirty line rather than writing
 * it back. Any neighbour sharing a line with the buffer loses whatever it had
 * just written. It happened here: FatFs's 512-byte window sat directly in front
 * of the SDMMC handle, so invalidating after a read threw away the handle's
 * Instance and Init fields that sdInit() had set moments earlier, and every
 * later HAL call went through a NULL Instance.
 *
 * So the buffer handed to DMA must not share a line with anything else. A
 * caller's buffer that is already line aligned is used directly; anything else
 * goes through a bounce buffer that is aligned by construction. Transfers are
 * always whole 512-byte sectors, so only the start address can be misaligned.
 * uSdHandle and the FATFS object are aligned too, which keeps them out of a
 * shared line no matter what a future caller does.
 *
 * The bounce buffer holds several sectors because the cost of the detour is not
 * the copy, it is losing the multi-block transfer: one sector per transaction
 * measured ten times slower on writes than one transaction for sixteen. At
 * eight sectors the misaligned path stays within reach of the direct one.
 */
static void cacheClean(const void *addr, uint32_t size)
{
  SCB_CleanDCache_by_Addr((uint32_t *)addr, (int32_t)size);
}

static void cacheInvalidate(const void *addr, uint32_t size)
{
  SCB_InvalidateDCache_by_Addr((uint32_t *)addr, (int32_t)size);
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
  while (sdIsBusy() == true)
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

#if HW_SD_USE_CMDIF == 1
void sdCmdifInit(void)
{
  if (cmdifIsInit() == false)
  {
    cmdifInit();
  }
  cmdifAdd("sd", sdCmdif);
}

void sdCmdif(void)
{
  bool ret = true;
  sd_info_t sd_info;


  if (cmdifGetParamCnt() == 1 && cmdifHasString("info", 0) == true)
  {
    cmdifPrintf("sd init      : %d\n", is_init);
    cmdifPrintf("sd connected : %d\n", sdIsDetected());

    if (is_init == true)
    {
      if (sdGetInfo(&sd_info) == true)
      {
        cmdifPrintf("  card_type            : %d\n", sd_info.card_type);
        cmdifPrintf("  card_version         : %d\n", sd_info.card_version);
        cmdifPrintf("  card_class           : %d\n", sd_info.card_class);
        cmdifPrintf("  rel_card_Add         : %d\n", sd_info.rel_card_Add);
        cmdifPrintf("  block_numbers        : %d\n", sd_info.block_numbers);
        cmdifPrintf("  block_size           : %d\n", sd_info.block_size);
        cmdifPrintf("  log_block_numbers    : %d\n", sd_info.log_block_numbers);
        cmdifPrintf("  log_block_size       : %d\n", sd_info.log_block_size);
        cmdifPrintf("  card_size            : %d MB, %d.%d GB\n", sd_info.card_size, sd_info.card_size/1024, ((sd_info.card_size * 10)/1024) % 10);
      }
    }
  }
  else
  {
    ret = false;
  }

  if (ret == false)
  {
    cmdifPrintf( "sd info \n");
  }
}
#endif /* _USE_HW_CMDIF_SD */

#endif /* _USE_HW_SD */
