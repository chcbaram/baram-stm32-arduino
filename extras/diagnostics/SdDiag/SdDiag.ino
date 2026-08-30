/*
 * SD bring-up diagnostics for WEACT-H750-MINI.
 *
 * Not an Arduino example on purpose - it lives under extras/ so it does not
 * appear in the IDE's example menu. Copy the folder into your sketchbook to
 * run it, or point arduino-cli at it directly:
 *
 *   arduino-cli compile -b baram-stm32-arduino:stm32:BARAM:pnum=WEACT_H750_MINI \
 *       extras/diagnostics/SdDiag --build-path /tmp/sddiag
 *   stm32/tools/baramdl/macosx/baramdl -write /tmp/sddiag/SdDiag.ino.bin
 *   cat /dev/cu.usbmodem*        # the port that is not the ST-LINK's
 *
 * Why this exists: the HAL reports several unrelated card-identification
 * failures as the single code HAL_SD_ERROR_UNSUPPORTED_FEATURE, and it also
 * mis-detects "no card at all". SD_PowerON() compares the CMD8 result against
 * SDMMC_ERROR_TIMEOUT (0x80000000), but SDMMC_CmdOperCond() returns
 * SDMMC_ERROR_CMD_RSP_TIMEOUT (0x00000004) when nothing answers. The two never
 * match, so an empty socket is recorded as a V2 card and the next command
 * (CMD55) is what appears to fail. Reading the handle afterwards tells you
 * "V2 card detected, CMD55 unsupported" when in truth nothing ever replied.
 *
 * So this drives the identification sequence by hand and prints what each
 * command actually did.
 *
 * How to read the output:
 *
 *   CMD0 CMDSENT + CMD8 CTIMEOUT   the host is fine, nothing is answering.
 *                                  Check that a card is seated, then try
 *                                  another card.
 *   CMD0 no CMDSENT                the command state machine is not running:
 *                                  kernel clock, POWER, or pin AF is wrong.
 *   CMD8 CCRCFAIL                  a card is answering but signal integrity or
 *                                  the clock divider is wrong.
 */
#include <WeActH750.h>
#include <SD.h>

static char buf[3072];
static int  len = 0;

static void logf(const char *fmt, ...)
{
  va_list ap; va_start(ap, fmt);
  len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
  va_end(ap);
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
}

// ---------------------------------------------------------------- clock tree

static void reportClocks(void)
{
  logf("clocks\n");
  logf("  SYSCLK=%lu HCLK=%lu  SDMMC kernel=%lu Hz\n",
       (unsigned long)HAL_RCC_GetSysClockFreq(),
       (unsigned long)HAL_RCC_GetHCLKFreq(),
       (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC));
  logf("  PLLCFGR=0x%08lX DIVP1EN=%lu DIVQ1EN=%lu DIVR1EN=%lu   D1CCIPR=0x%08lX\n",
       (unsigned long)RCC->PLLCFGR,
       (unsigned long)((RCC->PLLCFGR >> 16) & 1),
       (unsigned long)((RCC->PLLCFGR >> 17) & 1),
       (unsigned long)((RCC->PLLCFGR >> 18) & 1),
       (unsigned long)RCC->D1CCIPR);
  logf("  AHB3ENR=0x%08lX SDMMC1EN=%lu\n",
       (unsigned long)RCC->AHB3ENR, (unsigned long)((RCC->AHB3ENR >> 16) & 1));
}

// ---------------------------------------------------------------------- pins

static const char *modeName(uint32_t moder, int pin)
{
  switch ((moder >> (pin * 2)) & 3) {
    case 0:  return "IN ";
    case 1:  return "OUT";
    case 2:  return "AF ";
    default: return "AN ";
  }
}

static uint32_t afOf(GPIO_TypeDef *p, int pin)
{
  return pin < 8 ? ((p->AFR[0] >> (pin * 4)) & 0xF)
                 : ((p->AFR[1] >> ((pin - 8) * 4)) & 0xF);
}

static void dumpPin(const char *nm, GPIO_TypeDef *p, int pin)
{
  logf("  %-9s mode=%s af=%-2lu speed=%lu pupd=%lu idr=%lu\n", nm,
       modeName(p->MODER, pin), (unsigned long)afOf(p, pin),
       (unsigned long)((p->OSPEEDR >> (pin * 2)) & 3),
       (unsigned long)((p->PUPDR   >> (pin * 2)) & 3),
       (unsigned long)((p->IDR >> pin) & 1));
}

// ------------------------------------------------------------- raw commands

static void rawCmd(const char *nm, uint32_t idx, uint32_t arg, uint32_t wait_resp)
{
  SDMMC1->ICR = SDMMC_STATIC_CMD_FLAGS;
  SDMMC1->ARG = arg;
  SDMMC1->CMD = idx | wait_resp | SDMMC_CMD_CPSMEN;

  const uint32_t done = SDMMC_FLAG_CMDREND | SDMMC_FLAG_CTIMEOUT
                      | SDMMC_FLAG_CCRCFAIL | SDMMC_FLAG_CMDSENT;
  uint32_t t0 = millis(), sta;
  do {
    sta = SDMMC1->STA;
  } while (((sta & done) == 0) && (millis() - t0 < 200));

  logf("  %-9s STA=0x%08lX%s%s%s%s R1=0x%08lX RESPCMD=%lu\n", nm, (unsigned long)sta,
       (sta & SDMMC_FLAG_CMDSENT)  ? " CMDSENT"  : "",
       (sta & SDMMC_FLAG_CMDREND)  ? " CMDREND"  : "",
       (sta & SDMMC_FLAG_CTIMEOUT) ? " CTIMEOUT" : "",
       (sta & SDMMC_FLAG_CCRCFAIL) ? " CCRCFAIL" : "",
       (unsigned long)SDMMC1->RESP1, (unsigned long)SDMMC1->RESPCMD);

  SDMMC1->ICR = SDMMC_STATIC_CMD_FLAGS;
}

// --------------------------------------------------------------- pull test

// The card never drives CK and the SD wiring convention puts pull-ups only on
// CMD and DAT0..3. So CK is the control: if it follows the MCU's pull-down
// while the data lines do not, the high data lines are the board's own pull-ups
// and say nothing about whether a card is inserted.
static uint32_t readWith(GPIO_TypeDef *port, uint32_t pins, uint32_t pull)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = pins; g.Mode = GPIO_MODE_INPUT; g.Pull = pull; g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &g);
  delay(5);
  return port->IDR;
}

static void reportPulls(void)
{
  const uint32_t cpins = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  uint32_t dn = (readWith(GPIOC, cpins, GPIO_PULLDOWN) >> 8) & 0x1F;
  uint32_t up = (readWith(GPIOC, cpins, GPIO_PULLUP)   >> 8) & 0x1F;

  logf("pull test (CK D3 D2 D1 D0 = PC12..PC8)\n");
  logf("  pulldown: %lu %lu %lu %lu %lu\n",
       (unsigned long)((dn >> 4) & 1), (unsigned long)((dn >> 3) & 1),
       (unsigned long)((dn >> 2) & 1), (unsigned long)((dn >> 1) & 1),
       (unsigned long)(dn & 1));
  logf("  pullup  : %lu %lu %lu %lu %lu\n",
       (unsigned long)((up >> 4) & 1), (unsigned long)((up >> 3) & 1),
       (unsigned long)((up >> 2) & 1), (unsigned long)((up >> 1) & 1),
       (unsigned long)(up & 1));
  logf("  -> %s\n", ((dn >> 4) & 1)
       ? "CK is held high too: external pull-ups everywhere, test tells nothing"
       : "CK follows the MCU, so high data lines are board pull-ups, not a card");
}

void setup()
{
  board.begin(115200);

  // Everything below reads registers the driver owns, so let it run first.
  // Reading the clock tree before this would report SDMMC1EN=0 and look like a
  // fault when it only means nothing has enabled the peripheral yet.
  bool ok = sdInit();
  logf("sdInit()=%s  ErrorCode=0x%08lX   POWER=0x%08lX CLKCR=0x%08lX\n",
       ok ? "OK" : "FAIL", (unsigned long)sdGetLastError(),
       (unsigned long)SDMMC1->POWER, (unsigned long)SDMMC1->CLKCR);
  // CLKCR carries the answer on its own: the driver's own divider (a fast
  // transfer clock) means identification succeeded, because the HAL only
  // applies it at the end. The ~400 kHz identification divider means it did not.
  logf("\n");

  reportClocks();

  logf("\npins (expect mode=AF af=12 speed=3 pupd=1)\n");
  dumpPin("PC8  D0",  GPIOC, 8);
  dumpPin("PC9  D1",  GPIOC, 9);
  dumpPin("PC10 D2",  GPIOC, 10);
  dumpPin("PC11 D3",  GPIOC, 11);
  dumpPin("PC12 CK",  GPIOC, 12);
  dumpPin("PD2  CMD", GPIOD, 2);

  logf("\nraw command state machine\n");
  rawCmd("CMD0",  0,  0x00000000, 0);                      // no response expected
  delay(2);
  rawCmd("CMD8",  8,  0x000001AA, SDMMC_RESPONSE_SHORT);
  delay(2);
  rawCmd("CMD55", 55, 0x00000000, SDMMC_RESPONSE_SHORT);

  logf("\n");
  reportPulls();      // reconfigures the pins, so it goes last
}

void loop()
{
  Serial.print("\n===== SD DIAGNOSTICS =====\n");
  Serial.write((const uint8_t *)buf, len);
  Serial.print("==========================\n");
  board.ledToggle();
  delay(2000);
}
