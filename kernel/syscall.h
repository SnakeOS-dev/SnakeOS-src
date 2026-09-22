#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "idt.h"

#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_MMAP        9
#define SYS_BRK         12
#define SYS_SCHED_YIELD 24
#define SYS_EXIT        60

void syscall_init(void);
void syscall_handler(regs_t *r);

#endif
