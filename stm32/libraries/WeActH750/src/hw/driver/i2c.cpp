/*
 * i2c.cpp
 *
 * The bootloader's I2C calls, on top of the Arduino Wire library.
 *
 * See i2c.h for why this one stays on Wire while the SPI shim next door goes
 * down to the HAL.
 */

#include <Arduino.h>
#include <Wire.h>

extern "C" {
#include "hw/driver/i2c.h"
}

#ifdef _USE_HW_CAMERA

/*
 * The camera header's own bus.
 *
 * A separate TwoWire rather than the global Wire, because the header is on I2C1
 * (PB8/PB9) and Wire defaults to PB10/PB11, which is I2C2 - a different
 * peripheral. Constructing it here means a sketch's own Wire.begin() and this
 * never touch the same registers.
 */
static TwoWire cam_wire(HW_CAMERA_I2C_SDA, HW_CAMERA_I2C_SCL);
static bool    is_init = false;


bool i2cBegin(uint8_t ch, uint32_t freq_khz)
{
  if (ch != _DEF_I2C1) return false;

  if (is_init == false)
  {
    cam_wire.begin();
    is_init = true;
  }
  cam_wire.setClock(freq_khz * 1000);
  return true;
}

bool i2cIsInit(uint8_t ch)
{
  if (ch != _DEF_I2C1) return false;
  return is_init;
}

bool i2cIsDeviceReady(uint8_t ch, uint16_t dev_addr)
{
  if (ch != _DEF_I2C1 || is_init == false) return false;

  // An address cycle with no payload. endTransmission() reports 0 only when the
  // address was acknowledged, which is exactly the question being asked.
  cam_wire.beginTransmission((uint8_t)dev_addr);
  return cam_wire.endTransmission(true) == 0;
}

uint8_t i2cScan(uint8_t ch, uint8_t *p_found, uint8_t max_found)
{
  uint8_t n = 0;

  if (ch != _DEF_I2C1 || is_init == false || p_found == nullptr) return 0;

  // 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification.
  for (uint8_t addr = 0x08; addr <= 0x77 && n < max_found; addr++)
  {
    cam_wire.beginTransmission(addr);
    if (cam_wire.endTransmission(true) == 0)
    {
      p_found[n++] = addr;
    }
  }
  return n;
}

void i2cBusLevels(uint8_t ch, bool *p_sda, bool *p_scl)
{
  if (ch != _DEF_I2C1) return;

  // Read the pads directly. Wire has already claimed them as open drain
  // alternate function, and in that state the input buffer still follows the
  // line, so this reports what the bus is actually sitting at.
  if (p_sda) *p_sda = (digitalReadFast(digitalPinToPinName(HW_CAMERA_I2C_SDA)) == HIGH);
  if (p_scl) *p_scl = (digitalReadFast(digitalPinToPinName(HW_CAMERA_I2C_SCL)) == HIGH);
}

/*
 * timeout is accepted and not used.
 *
 * The core's Wire has no per-transaction timeout to hand it to - it uses its
 * own I2C_TIMEOUT_TICK for the whole transfer. Keeping the parameter means
 * ov7725.c stays byte for byte the file it was ported from, which is the point
 * of the shim; dropping it would mean editing every call site.
 */
bool i2cReadByte2(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout)
{
  (void)timeout;

  if (ch != _DEF_I2C1 || is_init == false || p_data == nullptr) return false;

  // Phase one: the register address, ended with a STOP. SCCB has no repeated
  // start - this is why the transfer is not a single Wire "write then read".
  cam_wire.beginTransmission((uint8_t)dev_addr);
  cam_wire.write((uint8_t)reg_addr);
  if (cam_wire.endTransmission(true) != 0) return false;

  // Phase two: a fresh START, one byte, STOP.
  if (cam_wire.requestFrom((uint8_t)dev_addr, (uint8_t)1) != 1) return false;
  if (cam_wire.available() < 1) return false;

  *p_data = (uint8_t)cam_wire.read();
  return true;
}

bool i2cWriteByte2(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout)
{
  (void)timeout;

  if (ch != _DEF_I2C1 || is_init == false) return false;

  cam_wire.beginTransmission((uint8_t)dev_addr);
  cam_wire.write((uint8_t)reg_addr);
  cam_wire.write(data);
  return cam_wire.endTransmission(true) == 0;
}

#endif /* _USE_HW_CAMERA */
