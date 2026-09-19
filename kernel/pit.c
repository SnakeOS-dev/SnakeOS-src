#include "pit.h"
#include "io.h"

static volatile uint64_t ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t div = PIT_FREQ / hz;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
    ticks = 0;
}

uint64_t pit_ticks(void) {
    return ticks;
}

void pit_tick(void) {
    ticks++;
}
