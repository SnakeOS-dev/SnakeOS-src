#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_print(const char *s);

#endif
