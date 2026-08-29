#pragma once

/*
 * The slice of the bootloader's GPIO API that its LCD driver uses, backed by
 * the Arduino pin API.
 *
 * The pin names themselves are in hw_def.h, next to the rest of the board
 * definitions, which is where the bootloader keeps them too.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes the level that means "on" for this pin, so callers can speak in terms
// of the function rather than the wiring. See the table in gpio_shim.cpp.
void gpioPinWrite(uint8_t ch, bool value);
bool gpioPinRead(uint8_t ch);

#ifdef __cplusplus
}
#endif
