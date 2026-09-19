#include "kernel.h"
#include "vga.h"
#include "idt.h"

void print(const char *s) { vga_print(s); }
void putchar(char c)      { vga_putchar(c); }

void print_hex(uint64_t v) {
    print("0x");
    for (int i = 60; i >= 0; i -= 4) {
        int d = (v >> i) & 0xF;
        putchar(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

void print_dec(uint64_t v) {
    char buf[32];
    int i = 0;
    if (v == 0) { putchar('0'); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) putchar(buf[i]);
}

void kernel_main(void) {
    vga_init();
    idt_init();

    print("Hello, World!\n");
    print("IDT loaded.\n");
    print("Testing IDT....\n");
    __asm__ volatile ("ud2");

    for (;;) __asm__ volatile ("hlt");
}
