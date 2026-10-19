#ifndef DOOM_TOASTY_CHANCE_H
#define DOOM_TOASTY_CHANCE_H
#include <stdint.h>
/* Cosmetic randomness must never advance Doom's gameplay/demo RNG. */
static inline int doom_toasty_roll(uint32_t *state) {
    uint32_t value;
    do { *state=*state*1664525u+1013904223u;value=*state; } while(value<16u);
    return value%20u==0;
}
#endif
