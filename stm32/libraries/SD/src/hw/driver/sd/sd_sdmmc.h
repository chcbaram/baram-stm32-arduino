/*
 * sd_sdmmc.h
 *
 * The SDMMC back end for sd.c. A board turns it on with _USE_HW_SDMMC and
 * describes the block and its pins with the HW_SD_* macros, both in hw_def.h.
 *
 * What a board has to define for this back end:
 *
 *   _USE_HW_SD                 the card layer at all
 *   _USE_HW_SDMMC              this back end drives it
 *
 *   HW_SD_INSTANCE             SDMMC1 or SDMMC2
 *   HW_SD_IRQn                 its interrupt
 *   HW_SD_IRQHandler           and the vector name
 *   HW_SD_CLK_ENABLE()         peripheral clock
 *   HW_SD_CLK_DISABLE()
 *   HW_SD_FORCE_RESET()        peripheral reset, used by DeInit
 *   HW_SD_RELEASE_RESET()
 *   HW_SD_AF                   alternate function for all six pins
 *
 *   HW_SD_BUS_PORT             the port carrying D0-D3 and CK
 *   HW_SD_BUS_PINS             their pin mask
 *   HW_SD_BUS_CLK_ENABLE()
 *   HW_SD_CMD_PORT             the port carrying CMD
 *   HW_SD_CMD_PINS
 *   HW_SD_CMD_CLK_ENABLE()
 *
 *   HW_SD_CLK_DIV              SDMMC_CK = kernel / (2 * this)
 *
 *   and for card detect, either
 *     HW_SD_DETECT_NONE        no usable detect line; a card is assumed present
 *   or all of
 *     HW_SD_DETECT_PORT / _PIN / _CLK_ENABLE() / _PRESENT
 *
 * Optional:
 *   HW_SD_BOUNCE_SECTORS       size of the misaligned-transfer buffer, default 8
 *
 * A board that splits D0-D3 and CK across two ports needs a small change here -
 * the bus group is configured as one HAL_GPIO_Init call.
 *
 * This back end is STM32H7 only. Another family shares the HAL_SD_* names but
 * not the DMA model or the cache calls underneath, so it gets its own file
 * beside this one; see the note at the top of sd_sdmmc.c. sd.c and everything
 * above it stay as they are - that is what the driver table is for.
 */

#ifndef SRC_COMMON_HW_INCLUDE_SD_SDMMC_H_
#define SRC_COMMON_HW_INCLUDE_SD_SDMMC_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw/driver/sd.h"

#ifdef _USE_HW_SDMMC

// Fills in the table sd.c dispatches through. The entries below are what it
// points at; nothing outside this back end should call them by name.
bool sdmmcInitDriver(sd_driver_t *p_driver);

bool     sdmmcInit(void);
bool     sdmmcDeInit(void);
bool     sdmmcIsDetected(void);
bool     sdmmcIsBusy(void);
bool     sdmmcRead(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool     sdmmcWrite(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool     sdmmcErase(uint32_t start_addr, uint32_t end_addr);
bool     sdmmcGetInfo(sd_info_t *p_info);
uint32_t sdmmcGetLastError(void);

#endif /* _USE_HW_SDMMC */

#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_SD_SDMMC_H_ */
