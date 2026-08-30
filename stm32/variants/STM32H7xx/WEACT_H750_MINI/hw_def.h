#pragma once

/*
 * Board definitions: what is on this board and where.
 *
 * This is the file to edit for a different board: pin names, panel size, panel
 * model. src/ is laid out the way the bootloader's source tree is - hw/hw_def.h
 * for the board, hw/driver/ for what runs on it - so a file moved between the
 * two projects only needs its include prefix adjusted.
 *
 * The driver, its fonts and the Hangul composer are used unmodified - that is
 * the point of this shim. Only the layer below them is different, so a sketch
 * renders exactly what the bootloader's splash screen does.
 *
 * Features the driver can optionally use (CLI, logging, PWM backlight, LVGL)
 * are deliberately left undefined, which compiles them out.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// The SD driver works through the HAL directly, so its types have to be here.
// stm32_def.h is the core's own way in and pulls the right family headers.
#include "stm32_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _DEF_LOW      0
#define _DEF_HIGH     1

// From the bootloader's def.h. constrain() is also an Arduino macro, but this
// file is included by C sources that do not see Arduino.h.
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#define cmax(a, b) (((a) > (b)) ? (a) : (b))
#define cmin(a, b) (((a) < (b)) ? (a) : (b))

// The drivers log and print through these. A sketch has no console guaranteed
// to exist, so they are compiled away.
#define logPrintf(...)     do { } while (0)
#define cmdifPrintf(...)   do { } while (0)

#define _DEF_SPI1     0

#define _USE_HW_GPIO
#define _USE_HW_LCD
#define _USE_HW_ST7735

// microSD on SDMMC1, 4 bit wide. _USE_HW_SD turns the card layer on and
// _USE_HW_SDMMC picks which back end drives it, the same way _USE_HW_LCD and
// _USE_HW_ST7735 pair up above.
#define _USE_HW_SD
#define _USE_HW_SDMMC

/*
 * Which SDMMC block and which pins. The driver reads these rather than naming
 * pins itself, so a board that wires the socket differently only edits this
 * file - the same rule the rest of hw_def.h follows.
 *
 * All six pins are AF12 on this part. D0-D3 are PC8-PC11 and CK is PC12, which
 * share a port and are configured together as the bus group; CMD is PD2.
 */
#define HW_SD_INSTANCE            SDMMC1
#define HW_SD_IRQn                SDMMC1_IRQn
#define HW_SD_IRQHandler          SDMMC1_IRQHandler
#define HW_SD_CLK_ENABLE()        __HAL_RCC_SDMMC1_CLK_ENABLE()
#define HW_SD_CLK_DISABLE()       __HAL_RCC_SDMMC1_CLK_DISABLE()
#define HW_SD_FORCE_RESET()       __HAL_RCC_SDMMC1_FORCE_RESET()
#define HW_SD_RELEASE_RESET()     __HAL_RCC_SDMMC1_RELEASE_RESET()
#define HW_SD_AF                  GPIO_AF12_SDIO1

#define HW_SD_BUS_PORT            GPIOC
#define HW_SD_BUS_PINS            (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12)  // D0-D3 + CK
#define HW_SD_BUS_CLK_ENABLE()    __HAL_RCC_GPIOC_CLK_ENABLE()

#define HW_SD_CMD_PORT            GPIOD
#define HW_SD_CMD_PINS            (GPIO_PIN_2)
#define HW_SD_CMD_CLK_ENABLE()    __HAL_RCC_GPIOD_CLK_ENABLE()

/*
 * SDMMC_CK = sdmmc_ker_ck / (2 * CLKDIV). The kernel is PLL1Q at 48 MHz, so 1
 * gives 24 MHz, just under the 25 MHz ceiling for default speed.
 *
 * The HAL's own SDMMC_HSPEED_CLK_DIV is 2, written for a 200 MHz kernel where
 * it lands on 50 MHz; here it would only give 12 MHz.
 *
 * 50 MHz would need two things, not one. The card has to be switched to high
 * speed with CMD6 - the HAL will do that through
 * HAL_SD_ConfigSpeedBusOperation() - and the kernel clock has to be fast enough
 * to divide down to it. This variant points SDMMC at PLL1Q, which USB also
 * uses and so is pinned at 48 MHz; reaching 50 MHz means moving SDMMC to PLL2R
 * in SystemClock_Config() first.
 */
#define HW_SD_CLK_DIV     1

/*
 * Card detect is not usable as the board ships.
 *
 * The socket's switch reaches PD4 only through solder bridge SB2, which is
 * open, and there is no external pull either way. Measured with a card in the
 * slot: PD4 reads high with the internal pull-up and low with the pull-down, so
 * it simply follows whichever pull is enabled - the pin is floating.
 *
 * So a card is assumed present and sdInit() failing is what reports an empty
 * slot. Closing SB2 makes detection real: swap the define below for the block
 * under it, and check the polarity - a card in should pull PD4 one way
 * regardless of the internal pull.
 */
#define HW_SD_DETECT_NONE

#if 0
#define HW_SD_DETECT_PORT         GPIOD
#define HW_SD_DETECT_PIN          GPIO_PIN_4
#define HW_SD_DETECT_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
#define HW_SD_DETECT_PRESENT      GPIO_PIN_RESET
#endif

#define HW_LCD_WIDTH      160
#define HW_LCD_HEIGHT     80
#define HW_LCD_LVGL       0

// Selects the 160x80 panel's offsets (col 1, row 26) and inverted colour mode.
#define HW_ST7735_MODEL   0

/*
 * DVP camera, on the board's camera header.
 *
 * Nothing here is a choice. The header is wired to DCMI in 8 bit mode, the
 * sensor's master clock comes out of MCO1 on PA8 and its SCCB bus is I2C1 -
 * all fixed by the board, and all already named in the variant's pin table.
 *
 * None of it collides with anything else on the board: the SD socket is on
 * PC8-PC12 and PD2, QSPI on PB2/PB6/PD11-PD13/PE2, the panel on SPI4
 * (PE11-PE14) and USB on PA11/PA12.
 */
#define _USE_HW_CAMERA

/*
 * Which sensor drivers are compiled in. Both are probed at run time and the
 * one that answers is used, so leaving both on costs only flash.
 *
 * The module that ships with this board is an OV2640 (SCCB 0x30, id 0x26).
 * The manufacturer also sells an OV7725 board for the same header, which is
 * why that driver stays.
 */
#define _USE_HW_OV2640
#define _USE_HW_OV7725

/*
 * QQVGA, which is what the manufacturer's example runs on this board and what
 * the panel wants anyway: 160 wide is the panel's width exactly, so a frame
 * needs cropping vertically and no scaling at all.
 */
#define HW_CAMERA_WIDTH           320
#define HW_CAMERA_HEIGHT          240
#define HW_CAMERA_FRAMESIZE       FRAMESIZE_QVGA

/*
 * The sensor's pixel clock divider (OV2640 CLKRC, sensor bank).
 *
 * 0 is undivided and is what the register tables leave behind; at that rate the
 * DCMI overruns near the end of every frame on this board. 1 halves it, which
 * is enough here and only costs frame rate.
 */
#define HW_CAMERA_CLKRC           1

/*
 * The sensor's master clock, out of MCO1.
 *
 * HSI48 divided by 4 is 12 MHz, which is what the manufacturer's own DCMI
 * example uses and what ov7725.c's exposure arithmetic assumes
 * (OMV_XCLK_FREQUENCY). HSI48 is off by the time a sketch runs - SystemInit()
 * clears HSI48ON and the variant's clock setup only turns HSE back on, because
 * USB takes its 48 MHz from PLL1Q - so cameraInit() enables it.
 *
 * PLL1Q would also give 12 MHz for free, but it is the system PLL: anything
 * that later retunes it moves the sensor's clock with it, silently. HSI48 keeps
 * the camera independent of the rest of the clock tree.
 */
#define HW_CAMERA_XCLK_HZ         12000000
#define HW_CAMERA_XCLK_PORT       GPIOA
#define HW_CAMERA_XCLK_PIN        GPIO_PIN_8
#define HW_CAMERA_XCLK_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()

/*
 * XCLK comes from TIM1, not MCO1.
 *
 * PA8 is both MCO1 and TIM1_CH1, and MCO1 is the obvious choice - it is what
 * the manufacturer's example uses by default. Their own source carries the
 * caveat, though: with an OV2640 driven from MCO1 the picture can come out
 * corrupted, and they offer a TIM1 channel 1 square wave as the alternative.
 * That matches what this board did - captures that reached about ninety percent
 * of a frame and then overran.
 *
 * Their note also says TIM1 collides with their LCD backlight PWM. It does not
 * here: this board's backlight is a plain GPIO (see LCD_BL in gpio.cpp), so
 * nothing else wants the timer.
 *
 * TIM1 sits on APB2. With this variant's tree - SYSCLK 480, HCLK 240, APB2 120 -
 * the timer clock is twice PCLK2, so 240 MHz; dividing by 20 gives the 12 MHz
 * the sensor's register tables assume.
 */
// #define HW_CAMERA_XCLK_TIM   /* see the note above; MCO1 is what delivers frames today */
#define HW_CAMERA_TIM             TIM1
#define HW_CAMERA_TIM_CH          TIM_CHANNEL_1
#define HW_CAMERA_TIM_CLK_HZ      240000000
#define HW_CAMERA_TIM_CLK_ENABLE()   __HAL_RCC_TIM1_CLK_ENABLE()
#define HW_CAMERA_XCLK_AF_TIM     GPIO_AF1_TIM1

/* The MCO1 route, kept for a sensor that is happy with it. */
#define HW_CAMERA_MCO_SOURCE      RCC_MCO1SOURCE_HSI48
#define HW_CAMERA_MCO_DIV         RCC_MCODIV_4
#define HW_CAMERA_XCLK_AF         GPIO_AF0_MCO

/* Data and sync, all AF13, spread over four ports. */
#define HW_CAMERA_AF              GPIO_AF13_DCMI

#define HW_CAMERA_PORTA_PINS      (GPIO_PIN_4 | GPIO_PIN_6)   /* HSYNC, PIXCLK */
#define HW_CAMERA_PORTB_PINS      (GPIO_PIN_7)                /* VSYNC         */
#define HW_CAMERA_PORTC_PINS      (GPIO_PIN_6 | GPIO_PIN_7)   /* D0, D1        */
#define HW_CAMERA_PORTD_PINS      (GPIO_PIN_3)                /* D5            */
#define HW_CAMERA_PORTE_PINS      (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 \
                                   | GPIO_PIN_5 | GPIO_PIN_6) /* D2,D3,D4,D6,D7 */

/*
 * Sync polarities, from the manufacturer's example for this board and the
 * OV7725 module that ships with it. Another reference driver for the same
 * sensor uses VSYNC active high; on this board that captures nothing.
 */
#define HW_CAMERA_VS_POLARITY     DCMI_VSPOLARITY_LOW
#define HW_CAMERA_HS_POLARITY     DCMI_HSPOLARITY_LOW
#define HW_CAMERA_PCK_POLARITY    DCMI_PCKPOLARITY_RISING

/*
 * DMA1 stream 0. Stream 1 is the panel's (SPI4 TX, see spi.cpp) and SDMMC uses
 * its own IDMA rather than a stream, so nothing else on this board competes.
 */
#define HW_CAMERA_DMA_STREAM      DMA1_Stream0
#define HW_CAMERA_DMA_IRQn        DMA1_Stream0_IRQn
#define HW_CAMERA_DMA_IRQHandler  DMA1_Stream0_IRQHandler
#define HW_CAMERA_DMA_REQUEST     DMA_REQUEST_DCMI

/*
 * SCCB. The camera header's I2C1 is a different peripheral from the pins Wire
 * defaults to (PB10/PB11 are I2C2), so a sketch can keep using Wire for its own
 * devices without touching the sensor.
 */
#define HW_CAMERA_I2C_SDA         PB9
#define HW_CAMERA_I2C_SCL         PB8
#define HW_CAMERA_I2C_FREQ        400000

/*
 * Power down reaches PA7 only through solder bridge SB1, which is open as the
 * board ships - the same situation as the card detect line above. The sensor is
 * therefore always powered; closing SB1 makes it controllable.
 */
#define HW_CAMERA_PWDN_NONE

#if 0
#define HW_CAMERA_PWDN_PORT       GPIOA
#define HW_CAMERA_PWDN_PIN        GPIO_PIN_7
#define HW_CAMERA_PWDN_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()
#endif

// GpioPinName_t in the bootloader. Only the LCD pins matter here, and the
// numbering has to match the table in gpio_shim.c.
typedef enum {
  LCD_CS = 0,
  LCD_DC,
  LCD_BL,
  GPIO_PIN_MAX
} GpioPinName_t;

// The driver calls these directly; Arduino provides them but not to C files
// that do not include Arduino.h.
void     delay(uint32_t ms);
uint32_t millis(void);

#ifdef __cplusplus
}
#endif
