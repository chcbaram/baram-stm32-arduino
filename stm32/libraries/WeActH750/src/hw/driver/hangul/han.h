/*
 * han.h
 *
 *  Created on: 2020. 12. 27.
 *      Author: baram
 */

#ifndef WEACT_H750_HANGUL_HAN_H_
#define WEACT_H750_HANGUL_HAN_H_

/*
 * Johab Hangul font, taken unchanged from the weact-h750-mini bootloader.
 *
 * Korean has 11,172 possible syllables, so a precomposed 16x16 font would run
 * to megabytes. This one stores the jamo instead - 8 sets of initials, 4 of
 * medials, 4 of finals - and composes each syllable at draw time, which fits
 * the whole language in about 80 KB.
 *
 * hanFontLoad() takes a pointer into a UTF-8 string, fills FontBuffer with a
 * 16x16 (Hangul) or 8x16 (ASCII) glyph, and returns how many bytes it consumed.
 */


#include <stdint.h>
#include <string.h>


#define PHAN_NULL_CODE    0
#define PHAN_HANGUL_CODE  1
#define PHAN_ENG_CODE     2
#define PHAN_SPEC_CODE    3
#define PHAN_END_CODE     4





typedef struct
{
  uint16_t HanCode;
  uint16_t Size_Char;  // 글자 1개의 바이트수(한글:2 영문:1)
  uint16_t Code_Type;  // 한/영/특수 문자인지 감별

  uint8_t  FontBuffer[32];
} han_font_t;



uint16_t hanFontLoad(char *HanCode, han_font_t *FontPtr);


#endif /* WEACT_H750_HANGUL_HAN_H_ */
