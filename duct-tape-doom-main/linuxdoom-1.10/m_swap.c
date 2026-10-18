#include "m_swap.h"
uint16_t SwapSHORT(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}
uint32_t SwapLONG(uint32_t value)
{
    return (value >> 24) | ((value >> 8) & UINT32_C(0x0000ff00))
         | ((value << 8) & UINT32_C(0x00ff0000)) | (value << 24);
}
