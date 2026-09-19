#include "idt.h"
#include "kernel.h"

static idt_entry_t idt[256];
static idtr_t idtr;

void idt_set_gate(uint8_t vec, void *handler, uint8_t ist, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;
    idt[vec].ist         = ist & 0x07;
    idt[vec].type_attr   = type_attr;
    idt[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero        = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0, 0x8E);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idtr));
}
