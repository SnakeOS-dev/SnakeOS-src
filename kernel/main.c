#include "kernel.h"
#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "isr.h"
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

static void timer_tick(void) {
    pit_tick();
}

static void kb_irq(void) {
    keyboard_handle();
}

void kernel_main(void) {
    vga_init();
    idt_init();

    pic_remap(IRQ_BASE, IRQ_BASE + 8);
    pic_set_mask(0xFFFF);

    pit_init(100);
    irq_register(0, timer_tick);
    pic_unmask(0);

    keyboard_init();
    irq_register(1, kb_irq);
    pic_unmask(1);
    __asm__ volatile ("sti");
    print("SnakeOS v0.00\n");
    print("\n");
    print("> ");

    for (;;) {
        int c = keyboard_getchar();
        if (c < 0) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        if (c == '\n') {
            print("\n> ");
        } else if (c == '\b') {
            print("\b \b");
        } else {
            putchar((char)c);
        }
    }
}
