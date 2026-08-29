#pragma once

/*
 * The STM32 EEPROM library is not available on these boards.
 *
 * It emulates EEPROM in the last sector of internal flash. On the STM32H750
 * there is exactly one sector - FLASH_SECTOR_TOTAL is 1 - so "the last sector"
 * is the entire 128 KB of internal flash, which is where the bootloader lives.
 * The upstream library compiles cleanly and without warnings, then erases the
 * bootloader on the first write:
 *
 *     HAL_FLASHEx_Erase(sector 0)      <- all 128 KB, the whole bootloader
 *     HAL_FLASH_Program(0x0801E000)    <- 8 KB of emulated EEPROM
 *
 * That leaves the board recoverable only over SWD or USB DFU, so the library
 * is replaced by this header rather than shipped.
 *
 * For persistent storage, use the SD card (SDMMC1) or a region of the external
 * QSPI flash past the end of the application. Note that the QSPI is memory
 * mapped for execute-in-place: writing to it means leaving memory-mapped mode,
 * which cannot be done from code that is itself executing from QSPI.
 */

#error "EEPROM is not available on this board - it would erase the bootloader. See the comment in this header for alternatives."
