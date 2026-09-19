#ifndef ISR_H
#define ISR_H

#include "idt.h"

typedef void (*irq_handler_t)(void);

void isr_handler(regs_t *r);
void irq_register(uint8_t irq, irq_handler_t fn);

#endif
