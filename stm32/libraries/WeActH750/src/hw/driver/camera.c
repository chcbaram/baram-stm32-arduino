/*
 * camera.c
 *
 *  Created on: 2020. 2. 12.
 *      Author: Baram
 */




#include "hw/driver/camera.h"
#include "hw/driver/i2c.h"
#include "hw/driver/ov7725.h"
#include "hw/driver/ov2640.h"

#ifdef _USE_HW_CAMERA

static uint8_t i2c_ch = _DEF_I2C1;

static camera_t   sensor = {0};
static DCMI_HandleTypeDef  hcamera_dcmi;
static bool is_init = false;
static bool is_requested = false;

const int resolution[][2] = {
    {0,    0   },
    // C/SIF Resolutions
    {88,   72  },    /* QQCIF     */
    {176,  144 },    /* QCIF      */
    {352,  288 },    /* CIF       */
    {88,   60  },    /* QQSIF     */
    {176,  120 },    /* QSIF      */
    {352,  240 },    /* SIF       */
    // VGA Resolutions
    {40,   30  },    /* QQQQVGA   */
    {80,   60  },    /* QQQVGA    */
    {160,  120 },    /* QQVGA     */
    {320,  240 },    /* QVGA      */
    {640,  480 },    /* VGA       */
    {60,   40  },    /* HQQQVGA   */
    {120,  80  },    /* HQQVGA    */
    {240,  160 },    /* HQVGA     */
    // FFT Resolutions
    {64,   32  },    /* 64x32     */
    {64,   64  },    /* 64x64     */
    {128,  64  },    /* 128x64    */
    {128,  128 },    /* 128x64    */
    // Other
    {128,  160 },    /* LCD       */
    {128,  160 },    /* QQVGA2    */
    {720,  480 },    /* WVGA      */
    {752,  480 },    /* WVGA2     */
    {800,  600 },    /* SVGA      */
    {1024, 768 },    /* XGA       */
    {1280, 1024},    /* SXGA      */
    {1600, 1200},    /* UXGA      */
    {1280, 720 },    /* HD        */
    {1920, 1080},    /* FHD       */
    {2560, 1440},    /* QHD       */
    {2048, 1536},    /* QXGA      */
    {2560, 1600},    /* WQXGA     */
    {2592, 1944},    /* WQXGA2    */
};




/*
 * Starts the sensor's master clock on MCO1.
 *
 * This has to happen before anything talks to the sensor: an OV7725 clocks its
 * SCCB logic from XCLK, so with no clock it does not even acknowledge its own
 * address and the probe below reports "no camera" on a camera that is present.
 *
 * HSI48 is off by the time a sketch runs - SystemInit() clears HSI48ON and the
 * variant's clock setup only turns HSE back on, because USB takes its 48 MHz
 * from PLL1Q - so it gets enabled here. Passing RCC_PLL_NONE means HAL touches
 * nothing else: PLL1 must not be disturbed, because the application executes in
 * place from QSPI and stopping PLL1 stops instruction fetch.
 *
 * Nothing here turns HSI48 back off, and nothing should. It is shared: the
 * bootloader clocks USB from it, and so does the board's own firmware. An
 * Arduino sketch happens to be the one case where the camera is its only
 * consumer - the variant clocks USB from PLL1Q instead - but a cameraDeInit()
 * that disabled it would take USB down with it on every other image, and the
 * symptom ("the serial port dies when the camera stops") points nowhere near an
 * oscillator. Stopping MCO1, or returning PA8 to analogue, is the way to shut
 * the sensor's clock off.
 */
static bool cameraXclkStart(void)
{
  RCC_OscInitTypeDef osc = {0};
  GPIO_InitTypeDef   gpio_init = {0};

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State     = RCC_HSI48_ON;
  osc.PLL.PLLState   = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    return false;
  }

  HW_CAMERA_XCLK_CLK_ENABLE();
  gpio_init.Pin       = HW_CAMERA_XCLK_PIN;
  gpio_init.Mode      = GPIO_MODE_AF_PP;
  gpio_init.Pull      = GPIO_NOPULL;
  gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = HW_CAMERA_XCLK_AF;
  HAL_GPIO_Init(HW_CAMERA_XCLK_PORT, &gpio_init);

  HAL_RCC_MCOConfig(RCC_MCO1, HW_CAMERA_MCO_SOURCE, HW_CAMERA_MCO_DIV);
  return true;
}

/*
 * Brings up the clock and the bus, then finds out what is plugged in.
 *
 * Which sensors can be found is a compile time choice in hw_def.h; which one is
 * actually there is a run time answer. The board's own module carries an
 * OV2640, but the manufacturer also sells an OV7725 board for the same header,
 * so both are probed rather than assumed - a mismatch would otherwise look
 * exactly like a missing camera.
 */
typedef struct
{
  uint8_t     slv_addr;
  uint8_t     id_reg;
  uint8_t     expect_id;
  const char *name;
  bool      (*init)(void);
  bool      (*open)(camera_t *sensor);
} camera_probe_t;

static const camera_probe_t camera_probe[] = {
#ifdef _USE_HW_OV2640
  // The id lives in a banked register set; reset() gets to it. Reading 0x0A
  // straight out of a cold OV2640 happens to return the same 0x26, which is
  // enough to tell it apart from an OV7725 at a different address anyway.
  { OV2640_SLV_ADDR, OV_CHIP_ID, OV2640_ID, "OV2640", ov2640Init, ov2640Open },
#endif
#ifdef _USE_HW_OV7725
  { OV7725_SLV_ADDR, OV_CHIP_ID, OV7725_ID, "OV7725", ov7725Init, ov7725Open },
#endif
};

bool cameraInit(void)
{
  if (cameraXclkStart() != true)
  {
    logPrintf("camera XCLK \t\t: Fail\n");
    return false;
  }

  for (uint32_t i = 0; i < sizeof(camera_probe) / sizeof(camera_probe[0]); i++)
  {
    const camera_probe_t *p_probe = &camera_probe[i];

    // Opens the SCCB bus. The sensor needs a few thousand XCLK cycles after the
    // clock appears before it answers, which at 12 MHz is well under a
    // millisecond - the delay is generous rather than measured. Without it the
    // probe below reports no camera on a camera that is simply not awake yet.
    p_probe->init();
    delay(10);

    if (i2cIsDeviceReady(i2c_ch, p_probe->slv_addr) != true)
    {
      continue;
    }

    sensor.slv_addr = p_probe->slv_addr;
    sensor.chip_id  = 0;

    if (i2cReadByte2(i2c_ch, sensor.slv_addr, p_probe->id_reg, &sensor.chip_id, 100) != true)
    {
      logPrintf("%s CHIP_ID \t\t: Fail\n", p_probe->name);
      continue;
    }
    logPrintf("%s CHIP_ID \t\t: 0x%X\n", p_probe->name, sensor.chip_id);

    if (p_probe->open(&sensor) != true)
    {
      logPrintf("%s Open \t\t: Fail\n", p_probe->name);
      continue;
    }

    cameraReset();
    cameraSetPixformat(PIXFORMAT_RGB565);
    cameraSetFramesize(FRAMESIZE_QVGA);

    is_init = true;
    return true;
  }

  return false;
}

/*
 * Power down, where the board routes it.
 *
 * On this one it does not: PWDN reaches PA7 only through solder bridge SB1,
 * which is open as the board ships, so the sensor is permanently enabled and
 * this is a no-op. sleep() over SCCB is the working alternative and is what
 * cameraSleep() uses. Closing SB1 and filling in the block in hw_def.h makes
 * this real.
 */
int cameraShutdown(int enable)
{
#ifdef HW_CAMERA_PWDN_NONE
  (void)enable;
#else
  HAL_GPIO_WritePin(HW_CAMERA_PWDN_PORT, HW_CAMERA_PWDN_PIN,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
  delay(10);
#endif
  return 0;
}

int cameraSetFramesize(framesize_t framesize)
{
  if (sensor.framesize == framesize) {
      // No change
      return 0;
  }

  // Call the sensor specific function
  if (sensor.set_framesize == NULL
      || sensor.set_framesize(&sensor, framesize) != 0) {
      // Operation not supported
      return -1;
  }

  // Set framebuffer size
  sensor.framesize = framesize;

  return 0;
}

int cameraSetFramerate(framerate_t framerate)
{
  if (sensor.framerate == framerate) {
     /* no change */
      return 0;
  }

  /* call the sensor specific function */
  if (sensor.set_framerate == NULL
      || sensor.set_framerate(&sensor, framerate) != 0) {
      /* operation not supported */
      return -1;
  }

  /* set the frame rate */
  sensor.framerate = framerate;

  return 0;
}

int cameraSetPixformat(pixformat_t pixformat)
{
  //uint32_t jpeg_mode = DCMI_JPEG_DISABLE;

  if (sensor.pixformat == pixformat) {
      // No change
      return 0;
  }

  if (sensor.set_pixformat == NULL
      || sensor.set_pixformat(&sensor, pixformat) != 0) {
      // Operation not supported
      return -1;
  }

  // Set pixel format
  sensor.pixformat = pixformat;

  // Set JPEG mode
  //if (pixformat == PIXFORMAT_JPEG) {
  //    jpeg_mode = DCMI_JPEG_ENABLE;
  //}

  // Skip the first frame.
  //MAIN_FB()->bpp = -1;

  //return dcmi_config(jpeg_mode);
  return 0;
}





static void DCMI_MspInit(DCMI_HandleTypeDef *hdcmi);
static void DCMI_MspDeInit(DCMI_HandleTypeDef *hdcmi);


bool cameraReset()
{
  cameraShutdown(true);
  delay(50);

  DCMI_MspInit(&hcamera_dcmi);

  /* DCMI configuration */
#if 0
  hcamera_dcmi.Instance              = DCMI;
  hcamera_dcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
  hcamera_dcmi.Init.HSPolarity       = HW_CAMERA_HS_POLARITY;
  hcamera_dcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
  hcamera_dcmi.Init.VSPolarity       = HW_CAMERA_VS_POLARITY;
  hcamera_dcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hcamera_dcmi.Init.PCKPolarity      = HW_CAMERA_PCK_POLARITY;
#else
  hcamera_dcmi.Instance              = DCMI;
  hcamera_dcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
  hcamera_dcmi.Init.HSPolarity       = HW_CAMERA_HS_POLARITY;
  hcamera_dcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
  hcamera_dcmi.Init.VSPolarity       = HW_CAMERA_VS_POLARITY;
  hcamera_dcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hcamera_dcmi.Init.PCKPolarity      = HW_CAMERA_PCK_POLARITY;
#endif

  hcamera_dcmi.Init.ByteSelectMode  = DCMI_BSM_ALL;         // Capture all received bytes
  hcamera_dcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;        // Ignored
  hcamera_dcmi.Init.LineSelectMode  = DCMI_LSM_ALL;         // Capture all received lines
  hcamera_dcmi.Init.LineSelectStart = DCMI_OELS_ODD;        // Ignored


  if(HAL_DCMI_Init(&hcamera_dcmi) != HAL_OK)
  {
    return false;
  }


  // Reset the sesnor state
  sensor.sde         = 0;
  sensor.pixformat   = 0;
  sensor.framesize   = 0;
  sensor.framerate   = 0;
  sensor.gainceiling = 0;

  cameraShutdown(false);
  delay(50);

  // Call sensor-specific reset function
  if (sensor.reset(&sensor) != 0) {
      return false;
  }

  return true;
}

static int32_t getBpp(pixformat_t pixformat)
{
  int32_t bpp = 0;


  switch(pixformat)
  {
    case PIXFORMAT_BINARY:
    case PIXFORMAT_GRAYSCALE:
    case PIXFORMAT_BAYER:
    case PIXFORMAT_JPEG:
      bpp = 1;
      break;

    case PIXFORMAT_RGB565:
    case PIXFORMAT_YUV422:
      bpp = 2;
      break;

    default:
      bpp = 0;
      break;
  }

  return bpp;
}

bool cameraIsAvailble(void)
{
  if (is_requested == true)
  {
    return false;
  }
  return true;
}

bool cameraStart(uint8_t *pBff, uint32_t Mode)
{
  bool ret = true;
  int32_t x_res;
  int32_t y_res;


  x_res = resolution[sensor.framesize][0];
  y_res = resolution[sensor.framesize][1];

  is_requested = true;

  if(HAL_DCMI_Start_DMA(&hcamera_dcmi, Mode, (uint32_t)pBff, (uint32_t)(x_res * y_res * getBpp(sensor.pixformat))/4) != HAL_OK)
  {
    ret = false;
  }

  return ret;
}


bool cameraStop(void)
{
  bool ret = true;

  if(HAL_DCMI_Stop(&hcamera_dcmi) != HAL_OK)
  {
    ret = false;
  }

  return ret;
}

bool cameraSuspend(void)
{
  if(HAL_DCMI_Suspend(&hcamera_dcmi) != HAL_OK)
  {
    return false;
  }
  return true;
}

bool cameraResume(void)
{
  if(HAL_DCMI_Resume(&hcamera_dcmi) != HAL_OK)
  {
    return false;
  }

  is_requested = true;
  return true;
}

bool cameraDeInit(void)
{
  bool ret = true;;

  hcamera_dcmi.Instance = DCMI;

  if(cameraStop() != true)
  {
    ret = false;
  }
  else if(HAL_DCMI_DisableCROP(&hcamera_dcmi)!= HAL_OK)
  {
    ret = false;
  }
  else if(HAL_DCMI_DeInit(&hcamera_dcmi) != HAL_OK)
  {
    ret = false;
  }
  else
  {
    DCMI_MspDeInit(&hcamera_dcmi);
  }

  return ret;
}


/**
  * @brief  Initializes the DCMI MSP.
  * @param  hdcmi  DCMI handle
  * @retval None
  */
/*
 * Brings up everything the DCMI needs that is not the DCMI: the pins, the DMA
 * stream and the interrupts.
 *
 * Which pins and which stream come from hw_def.h, so a board that wires the
 * camera header differently only edits that file - the same rule the SD driver
 * follows. All of the data and sync pins are AF13 on this part; only the ports
 * differ, which is why they are grouped by port rather than by signal.
 */
static void DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
  static DMA_HandleTypeDef hdma_handler;
  GPIO_InitTypeDef gpio_init = {0};

  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  gpio_init.Mode      = GPIO_MODE_AF_PP;
  gpio_init.Pull      = GPIO_NOPULL;
  gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = HW_CAMERA_AF;

  gpio_init.Pin = HW_CAMERA_PORTA_PINS;   HAL_GPIO_Init(GPIOA, &gpio_init);
  gpio_init.Pin = HW_CAMERA_PORTB_PINS;   HAL_GPIO_Init(GPIOB, &gpio_init);
  gpio_init.Pin = HW_CAMERA_PORTC_PINS;   HAL_GPIO_Init(GPIOC, &gpio_init);
  gpio_init.Pin = HW_CAMERA_PORTD_PINS;   HAL_GPIO_Init(GPIOD, &gpio_init);
  gpio_init.Pin = HW_CAMERA_PORTE_PINS;   HAL_GPIO_Init(GPIOE, &gpio_init);

  /*
   * Circular mode, so the sensor keeps filling the same buffer frame after
   * frame and a sketch never has to re-arm anything. Word alignment on both
   * sides is what lets HAL_DCMI_Start_DMA take a length in words.
   */
  hdma_handler.Instance                 = HW_CAMERA_DMA_STREAM;
  hdma_handler.Init.Request             = HW_CAMERA_DMA_REQUEST;
  hdma_handler.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_handler.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_handler.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_handler.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_handler.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  hdma_handler.Init.Mode                = DMA_CIRCULAR;
  hdma_handler.Init.Priority            = DMA_PRIORITY_HIGH;
  hdma_handler.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
  hdma_handler.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  hdma_handler.Init.MemBurst            = DMA_MBURST_SINGLE;
  hdma_handler.Init.PeriphBurst         = DMA_PBURST_SINGLE;

  __HAL_LINKDMA(hdcmi, DMA_Handle, hdma_handler);

  HAL_NVIC_SetPriority(DCMI_IRQn, 0x05, 0);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);

  HAL_NVIC_SetPriority(HW_CAMERA_DMA_IRQn, 0x05, 0);
  HAL_NVIC_EnableIRQ(HW_CAMERA_DMA_IRQn);

  (void)HAL_DMA_Init(hdcmi->DMA_Handle);
}

static void DCMI_MspDeInit(DCMI_HandleTypeDef *hdcmi)
{
  HAL_NVIC_DisableIRQ(DCMI_IRQn);
  HAL_NVIC_DisableIRQ(HW_CAMERA_DMA_IRQn);

  __HAL_RCC_DCMI_CLK_DISABLE();

  HAL_GPIO_DeInit(GPIOA, HW_CAMERA_PORTA_PINS);
  HAL_GPIO_DeInit(GPIOB, HW_CAMERA_PORTB_PINS);
  HAL_GPIO_DeInit(GPIOC, HW_CAMERA_PORTC_PINS);
  HAL_GPIO_DeInit(GPIOD, HW_CAMERA_PORTD_PINS);
  HAL_GPIO_DeInit(GPIOE, HW_CAMERA_PORTE_PINS);

  HAL_DMA_DeInit(hdcmi->DMA_Handle);
}


void DCMI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&hcamera_dcmi);

  if (hcamera_dcmi.ErrorCode > 0)
  {
    logPrintf("DCMI Err %d\n", (int)hcamera_dcmi.ErrorCode);
  }

}

void HW_CAMERA_DMA_IRQHandler(void)
{
  HAL_DMA_IRQHandler(hcamera_dcmi.DMA_Handle);
  if (hcamera_dcmi.DMA_Handle->ErrorCode > 0)
  {
    logPrintf("DMA Err %d\n", (int)hcamera_dcmi.DMA_Handle->ErrorCode);
  }
}

/**
  * @brief  Line event callback
  * @param  hdcmi  pointer to the DCMI handle
  * @retval None
  */
void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hdcmi);
  //BSP_CAMERA_LineEventCallback(0);
}

/**
  * @brief  Frame event callback
  * @param  hdcmi pointer to the DCMI handle
  * @retval None
  */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hdcmi);


  static uint32_t pre_time;
  static int32_t fps;
  static int32_t time;



  time = millis()-pre_time;
  if (time > 0)
  {
    fps = 1000/time;
  }
  logPrintf("%d ms,  %d fps\n", time, fps);
  pre_time = millis();

  is_requested = false;
  //BSP_CAMERA_FrameEventCallback(0);
}

/**
  * @brief  Vsync event callback
  * @param  hdcmi pointer to the DCMI handle
  * @retval None
  */
void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hdcmi);
}

/**
  * @brief  Error callback
  * @param  hdcmi pointer to the DCMI handle
  * @retval None
  */
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hdcmi);

  logPrintf("error %d\n", hdcmi->ErrorCode);
  HAL_DCMI_DeInit(&hcamera_dcmi);
  HAL_DCMI_Init(&hcamera_dcmi);
  //BSP_CAMERA_ErrorCallback(0);
}

#endif /* _USE_HW_CAMERA */
