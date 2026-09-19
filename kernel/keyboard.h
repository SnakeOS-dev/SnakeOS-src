#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
int  keyboard_getchar(void);
void keyboard_handle(void);
#endif
