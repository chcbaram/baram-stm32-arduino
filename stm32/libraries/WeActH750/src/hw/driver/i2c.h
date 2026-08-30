#pragma once

/*
 * The slice of the bootloader's I2C API that the camera's SCCB bus needs,
 * backed by the Arduino Wire library.
 *
 * Unlike the SPI shim next door, this one does not go down to the HAL. It does
 * not need to: the panel forced that because the core's SPI library refuses a
 * bus with no MISO, and Wire has no equivalent restriction.
 *
 * The camera header is on I2C1 (PB8/PB9), which is a different peripheral from
 * the pins Wire defaults to (PB10/PB11 are I2C2). This shim therefore owns its
 * own TwoWire instance and a sketch can keep using Wire for its own devices.
 *
 * The pins and the bus speed are in hw_def.h with the rest of the board.
 */

#include "hw_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _DEF_I2C1     0

bool i2cBegin(uint8_t ch, uint32_t freq_khz);
bool i2cIsInit(uint8_t ch);

// Probes for a device: a bare address cycle that the device either acknowledges
// or does not. cameraInit() uses it to tell "no module plugged in" apart from
// "module present but not answering", which is worth separating in a sketch.
bool i2cIsDeviceReady(uint8_t ch, uint16_t dev_addr);

// Walks the 7 bit address space and reports what answers, up to max_found
// entries. Tells "the sensor is at a different address" apart from "nothing is
// on this bus at all", which the single-address probe above cannot.
uint8_t i2cScan(uint8_t ch, uint8_t *p_found, uint8_t max_found);

// The idle levels of SDA and SCL, read as plain inputs before the peripheral
// takes the pins. Both should be high: the module carries the pull-ups. A low
// line means no pull-up, nothing plugged in, or a device holding the bus.
void i2cBusLevels(uint8_t ch, bool *p_sda, bool *p_scl);

/*
 * Read and write one register.
 *
 * dev_addr is the 7 bit address, the way the sensor driver keeps it
 * (OV7725_SLV_ADDR is 0x42/2). Wire shifts it.
 *
 * The read is deliberately two transactions with a STOP between them - write
 * the register address, stop, then read - rather than the repeated START that
 * HAL_I2C_Mem_Read would issue. That is what SCCB specifies, and it is what the
 * bootloader's own i2cReadByte2 does with two HAL_I2C_Master_ calls. Arduino's
 * endTransmission() ends with a STOP by default, so the plain Wire sequence is
 * already the right one.
 */
bool i2cReadByte2(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout);
bool i2cWriteByte2(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout);

#ifdef __cplusplus
}
#endif
