/* WAD byte order is always little endian. */
#ifndef DOOM_M_SWAP_H
#define DOOM_M_SWAP_H
#include <stdint.h>
uint16_t SwapSHORT(uint16_t value);
uint32_t SwapLONG(uint32_t value);
#if defined(DOOM_BIG_ENDIAN) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define SHORT(x) ((int16_t)SwapSHORT((uint16_t)(x)))
#define LONG(x) ((int32_t)SwapLONG((uint32_t)(x)))
#else
#define SHORT(x) ((int16_t)(x))
#define LONG(x) ((int32_t)(x))
#endif
#endif
