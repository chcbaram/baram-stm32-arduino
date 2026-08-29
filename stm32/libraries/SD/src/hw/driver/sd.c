/*
 * led.c
 *
 *  Created on: 2017. 2. 13.
 *      Author: baram
 */





#include "hw/driver/sd.h"


#ifdef _USE_HW_SD
/* Card detect would come from the board's GPIO layer. This board has no detect
 * line - HW_SD_DETECT_NONE in hw_def.h - so nothing is needed here. A board
 * that has one includes its own GPIO header. */



//-- Internal Variables
//
static bool is_init = false;
static volatile bool is_rx_done = false;
static volatile bool is_tx_done = false;

/*
 * Cache maintenance around DMA.
 *
 * The core turns the D-cache on, and the SDMMC's internal DMA writes straight
 * to memory. Without invalidating after a read the CPU keeps serving stale
 * cache lines; without cleaning before a write the card gets stale memory.
 *
 * Both operate on whole 32-byte lines, so a range that does not start and end
 * on a line boundary would drag in neighbouring data. FatFs hands over sector
 * buffers that are 512 bytes and word aligned, and the caller's own buffers can
 * be anything, so the range is widened to line boundaries. That is safe for
 * invalidate-after-read only because the surrounding bytes belong to the same
 * buffer or are not live; a bounce buffer would be the alternative, at the cost
 * of a copy per sector.
 */
#define CACHE_LINE  32

static void cacheCleanRange(const void *addr, uint32_t size)
{
  uint32_t start = (uint32_t)addr & ~(CACHE_LINE - 1);
  uint32_t end   = ((uint32_t)addr + size + CACHE_LINE - 1) & ~(CACHE_LINE - 1);
  SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void cacheInvalidateRange(const void *addr, uint32_t size)
{
  uint32_t start = (uint32_t)addr & ~(CACHE_LINE - 1);
  uint32_t end   = ((uint32_t)addr + size + CACHE_LINE - 1) & ~(CACHE_LINE - 1);
  SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

// Waits for a DMA completion flag, then for the card to leave its busy state.
static bool sdWaitDone(volatile bool *p_done, uint32_t timeout_ms)
{
  uint32_t pre_time = millis();

  while (*p_done == false)
  {
    if (millis() - pre_time >= timeout_ms) return false;
  }
  while (sdIsBusy() == true)
  {
    if (millis() - pre_time >= timeout_ms) return false;
  }
  return true;
}
static SD_HandleTypeDef uSdHandle;




//-- External Variables
//


//-- Internal Functions
//
#if HW_SD_USE_CMDIF == 1
void sdCmdifInit(void);
void sdCmdif(void);
#endif

//static void sdInitHw(void);


//-- External Functions
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
  uSdHandle.Init.ClockDiv            = SDMMC_HSpeed_CLK_DIV;



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


bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;

  if (is_init == false) return false;

  is_rx_done = false;

  if (HAL_SD_ReadBlocks_DMA(&uSdHandle, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  {
    ret = sdWaitDone(&is_rx_done, timeout_ms);
  }

  if (ret == true)
  {
    cacheInvalidateRange(p_data, num_of_blocks * BLOCKSIZE);
  }

  return ret;
}

bool sdWriteBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;

  if (is_init == false) return false;

  // Push the data out of the cache before the DMA reads it.
  cacheCleanRange(p_data, num_of_blocks * BLOCKSIZE);

  is_tx_done = false;

  if (HAL_SD_WriteBlocks_DMA(&uSdHandle, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  {
    ret = sdWaitDone(&is_tx_done, timeout_ms);
  }

  return ret;
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

void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
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
