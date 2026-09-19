#include "vga.h"

static uint16_t *const vga = (uint16_t *)VGA_MEMORY;
static size_t row = 0;
static size_t col = 0;
static uint8_t color = 0x0F;

static void vga_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)color << 8);
}

void vga_init(void) {
    row = 0;
    col = 0;
    vga_clear();
}

void vga_clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (uint16_t)' ' | ((uint16_t)color << 8);
    row = 0;
    col = 0;
}

void vga_putchar(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
        return;
    } else if (c == '\t') {
        col = (col + 8) & ~(size_t)7;
        if (col >= VGA_WIDTH) { col = 0; row++; }
    } else {
        vga[row * VGA_WIDTH + col] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
        col++;
        if (col >= VGA_WIDTH) { col = 0; row++; }
    }
    if (row >= VGA_HEIGHT) {
        vga_scroll();
        row = VGA_HEIGHT - 1;
    }
}

void vga_print(const char *s) {
    while (*s) vga_putchar(*s++);
}
