#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182

void pit_init(uint32_t hz);
uint64_t pit_ticks(void);
void pit_tick(void);
#endif
