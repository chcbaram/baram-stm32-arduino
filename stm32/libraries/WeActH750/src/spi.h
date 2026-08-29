#pragma once

/*
 * The slice of the bootloader's SPI API that its LCD driver uses, backed by the
 * Arduino SPI library.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_MODE0   0
#define SPI_MODE1   1
#define SPI_MODE2   2
#define SPI_MODE3   3

bool spiBegin(uint8_t ch);
void spiSetDataMode(uint8_t ch, uint8_t dataMode);
void spiSetBitWidth(uint8_t ch, uint8_t bit_width);
uint8_t spiTransfer8(uint8_t ch, uint8_t data);
void spiAttachTxInterrupt(uint8_t ch, void (*func)(void));
bool spiDmaTxTransfer(uint8_t ch, void *buf, uint32_t length, uint32_t timeout);

#ifdef __cplusplus
}
#endif
