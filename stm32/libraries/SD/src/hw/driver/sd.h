/*
 * sd.h
 *
 * A block device holding a FAT filesystem, described the way a caller wants to
 * see it: initialise, read and write 512-byte blocks, ask whether a card is
 * there. Nothing here names a peripheral.
 *
 * A board says which back end fills the table by defining one of the
 * _USE_HW_* switches in its hw_def.h. Today that is _USE_HW_SDMMC; a board
 * whose socket hangs off SPI would add a back end beside it and change nothing
 * above this line.
 *
 * A board with no card socket at all simply defines neither. Everything still
 * compiles and sdInit() reports failure, so a sketch that opens a file gets a
 * runtime "no card" rather than a build error - which is what lets one library
 * serve every board in the package.
 */

#ifndef SRC_COMMON_HW_INCLUDE_SD_H_
#define SRC_COMMON_HW_INCLUDE_SD_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


typedef struct
{
  uint32_t card_type;                    /*!< Specifies the card Type                         */
  uint32_t card_version;                 /*!< Specifies the card version                      */
  uint32_t card_class;                   /*!< Specifies the class of the card class           */
  uint32_t rel_card_Add;                 /*!< Specifies the Relative Card Address             */
  uint32_t block_numbers;                /*!< Specifies the Card Capacity in blocks           */
  uint32_t block_size;                   /*!< Specifies one block size in bytes               */
  uint32_t log_block_numbers;            /*!< Specifies the Card logical Capacity in blocks   */
  uint32_t log_block_size;               /*!< Specifies logical block size in bytes           */
  uint32_t card_size;
} sd_info_t;


typedef struct sd_driver_t_ sd_driver_t;

typedef struct sd_driver_t_
{
  bool     (*init)(void);
  bool     (*deinit)(void);
  bool     (*isDetected)(void);
  bool     (*isBusy)(void);
  bool     (*read)(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
  bool     (*write)(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
  bool     (*erase)(uint32_t start_addr, uint32_t end_addr);
  bool     (*getInfo)(sd_info_t *p_info);
  uint32_t (*getLastError)(void);

} sd_driver_t;


bool sdInit(void);
bool sdDeInit(void);

// True once a back end is present and its card has been identified. False on a
// board with no socket, so a caller can tell "not supported" from "no card" by
// pairing this with sdIsDetected().
bool sdIsInit(void);

bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool sdWriteBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms);
bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr);
bool sdIsBusy(void);
bool sdIsDetected(void);
bool sdGetInfo(sd_info_t *p_info);

// Whatever the back end last failed with, for reporting why sdInit() said no.
// Zero when there is no back end.
uint32_t sdGetLastError(void);


#ifdef __cplusplus
}
#endif


#endif /* SRC_COMMON_HW_INCLUDE_SD_H_ */
