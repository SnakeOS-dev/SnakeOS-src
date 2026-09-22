#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_DS   0x23
#define USER_CS   0x2B
#define TSS_SEL   0x30

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
