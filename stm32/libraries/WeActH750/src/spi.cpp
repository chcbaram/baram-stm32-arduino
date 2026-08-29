/*
 * The bootloader's SPI calls, driving SPI4 through the HAL directly.
 *
 * The core's SPI library cannot be used here. Its spi_transfer() waits for a
 * received byte after every byte it sends, even when asked to skip receiving,
 * and refuses to initialise at all when no MISO pin is given
 * (spi_com.c: "spi_mosi == NP || spi_miso == NP || spi_sclk == NP"). This
 * panel has no MISO - the board does not route one - so a sketch using SPIClass
 * hangs forever inside spi_transfer on an uninitialised peripheral. The
 * alternative would be to hand it an unrelated pin as a dummy MISO, which
 * quietly takes that pin away from the sketch.
 *
 * Going straight to the HAL avoids both, and puts the transfer in one place
 * where DMA can replace the blocking call later.
 *
 * Frames go out over DMA, which is what the driver was written for:
 * spiDmaTxTransfer() starts the transfer and returns, and the completion
 * interrupt raises chip select and tells the driver the frame is done.
 * lcdDrawAvailable() is false in between, so a sketch can compute the next
 * frame during the 5 ms the previous one takes on the wire instead of waiting
 * for it.
 *
 * Commands and their one-byte arguments stay blocking (spiTransfer8): setting
 * up a DMA transfer costs more than sending a single byte.
 *
 * The framebuffer lives in .non_cache at 0x30000000, in D2 SRAM, which is both
 * reachable by DMA1 - unlike DTCM - and mapped non-cacheable by the MPU, so
 * there is no cache maintenance to get wrong.
 */

#include <Arduino.h>

extern "C" {
#include "spi.h"
}

// SPI4 on this board: PE12 SCK, PE14 MOSI. No MISO, no hardware NSS - chip
// select is a plain GPIO so the driver can hold it across a whole frame.
#define LCD_SPI            SPI4
#define LCD_SPI_AF         GPIO_AF5_SPI4
#define LCD_SPI_SCK_PIN    GPIO_PIN_12
#define LCD_SPI_MOSI_PIN   GPIO_PIN_14
#define LCD_SPI_PORT       GPIOE

static SPI_HandleTypeDef hspi;
static DMA_HandleTypeDef hdma_tx;
static void (*txCallback)(void) = nullptr;
static bool begun = false;

// DMA1 Stream 1, the stream the bootloader uses for the same job. Nothing in
// the core claims it: the startup file leaves DMA1_Stream1_IRQHandler and
// SPI4_IRQHandler weak and no core file overrides them.
#define LCD_DMA_STREAM     DMA1_Stream1
#define LCD_DMA_IRQn       DMA1_Stream1_IRQn
#define LCD_DMA_REQUEST    DMA_REQUEST_SPI4_TX
#define LCD_SPI_IRQn       SPI4_IRQn

extern "C" bool spiBegin(uint8_t ch)
{
  (void)ch;
  if (begun) return true;

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_SPI4_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin       = LCD_SPI_SCK_PIN | LCD_SPI_MOSI_PIN;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = LCD_SPI_AF;
  HAL_GPIO_Init(LCD_SPI_PORT, &gpio);

  hspi.Instance               = LCD_SPI;
  hspi.Init.Mode              = SPI_MODE_MASTER;
  // Transmit only: the panel never talks back, and this stops the HAL from
  // waiting on a receive that cannot happen.
  hspi.Init.Direction         = SPI_DIRECTION_2LINES_TXONLY;
  hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;   // mode 0
  hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;
  hspi.Init.NSS               = SPI_NSS_SOFT;
  // SPI4's kernel clock is PLL3 at 80 MHz (see the variant), so /2 gives
  // 40 MHz - within the panel's rating and enough for about 195 frames a
  // second if nothing else were in the way.
  hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
  hspi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

  if (HAL_SPI_Init(&hspi) != HAL_OK) return false;

  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_tx.Instance                 = LCD_DMA_STREAM;
  hdma_tx.Init.Request             = LCD_DMA_REQUEST;
  hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  hdma_tx.Init.Mode                = DMA_NORMAL;
  hdma_tx.Init.Priority            = DMA_PRIORITY_HIGH;
  hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

  if (HAL_DMA_Init(&hdma_tx) != HAL_OK) return false;
  __HAL_LINKDMA(&hspi, hdmatx, hdma_tx);

  // Below the systick priority so millis() keeps ticking through a frame.
  HAL_NVIC_SetPriority(LCD_DMA_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(LCD_DMA_IRQn);
  HAL_NVIC_SetPriority(LCD_SPI_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(LCD_SPI_IRQn);

  begun = true;
  return true;
}

extern "C" void DMA1_Stream1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_tx);
}

extern "C" void SPI4_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hspi);
}

// Called from the interrupt once the last frame has left the shift register.
// The driver raises chip select and marks the frame done from here.
extern "C" void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *h)
{
  if (h != &hspi) return;
  if (txCallback != nullptr) txCallback();
}

extern "C" void spiSetDataMode(uint8_t ch, uint8_t dataMode)
{
  (void)ch;
  if (!begun) return;

  uint32_t pol = SPI_POLARITY_LOW, pha = SPI_PHASE_1EDGE;
  switch (dataMode) {
    case SPI_MODE1: pol = SPI_POLARITY_LOW;  pha = SPI_PHASE_2EDGE; break;
    case SPI_MODE2: pol = SPI_POLARITY_HIGH; pha = SPI_PHASE_1EDGE; break;
    case SPI_MODE3: pol = SPI_POLARITY_HIGH; pha = SPI_PHASE_2EDGE; break;
    default: break;
  }
  if (hspi.Init.CLKPolarity == pol && hspi.Init.CLKPhase == pha) return;

  hspi.Init.CLKPolarity = pol;
  hspi.Init.CLKPhase    = pha;
  HAL_SPI_Init(&hspi);
}

extern "C" void spiSetBitWidth(uint8_t ch, uint8_t bit_width)
{
  // This is not cosmetic. The driver switches to 16 bits before pushing pixels
  // and then passes spiDmaTxTransfer() a length in *frames*, not bytes - a full
  // 160x80 screen is 12800 - so ignoring the width sends half the image.
  //
  // The width also fixes the byte order. The framebuffer holds native
  // little-endian uint16 values while the panel wants the high byte first; a
  // 16-bit frame goes out MSB first, which lines the two up. Sending the same
  // memory as bytes would swap every pixel's halves.
  (void)ch;
  if (!begun) return;

  uint32_t size = (bit_width == 16) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
  if (hspi.Init.DataSize == size) return;

  hspi.Init.DataSize = size;
  HAL_SPI_Init(&hspi);

  // The DMA has to move the same unit the SPI consumes, or every second byte
  // is dropped.
  uint32_t align = (bit_width == 16) ? DMA_PDATAALIGN_HALFWORD : DMA_PDATAALIGN_BYTE;
  if (hdma_tx.Init.PeriphDataAlignment != align) {
    hdma_tx.Init.PeriphDataAlignment = align;
    hdma_tx.Init.MemDataAlignment    = (bit_width == 16) ? DMA_MDATAALIGN_HALFWORD
                                                         : DMA_MDATAALIGN_BYTE;
    HAL_DMA_Init(&hdma_tx);
  }
}

extern "C" uint8_t spiTransfer8(uint8_t ch, uint8_t data)
{
  (void)ch;
  if (!begun) return 0;
  HAL_SPI_Transmit(&hspi, &data, 1, 100);
  return 0;  // transmit-only: nothing comes back
}

extern "C" void spiAttachTxInterrupt(uint8_t ch, void (*func)(void))
{
  (void)ch;
  txCallback = func;
}

extern "C" bool spiDmaTxTransfer(uint8_t ch, void *buf, uint32_t length, uint32_t timeout)
{
  (void)ch;
  (void)timeout;   // asynchronous: there is nothing here to wait for
  if (!begun || buf == nullptr || length == 0) return false;

  // length counts data frames - bytes at 8 bits, halfwords at 16 - which is
  // also what HAL_SPI_Transmit_DMA's Size means. A 160x80 screen is 12800
  // halfwords, inside the 16-bit limit, so no chunking is needed.
  if (length > 0xFFFF) return false;

  return HAL_SPI_Transmit_DMA(&hspi, (uint8_t *)buf, (uint16_t)length) == HAL_OK;
}
