#include "keyboard.h"
#include "io.h"

#define KB_DATA   0x60
#define KB_STATUS 0x64

#define KB_BUF_SIZE 256

static const char keymap[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0, ' ',
};

static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0, ' ',
};

static volatile char buf[KB_BUF_SIZE];
static volatile int  head = 0;
static volatile int  tail = 0;
static int shift = 0;
static int caps  = 0;
static int extended = 0;

static void buf_push(char c) {
    int next = (head + 1) % KB_BUF_SIZE;
    if (next == tail) return;
    buf[head] = c;
    head = next;
}

void keyboard_init(void) {
    while (inb(KB_STATUS) & 0x01) inb(KB_DATA);
    head = tail = 0;
    shift = caps = extended = 0;
}

void keyboard_handle(void) {
    uint8_t sc = inb(KB_DATA);

    if (sc == 0xE0) { extended = 1; return; }

    if (sc & 0x80) {
        uint8_t code = sc & 0x7F;
        if (code == 0x2A || code == 0x36) shift = 0;
        extended = 0;
        return;
    }

    if (extended) {
        extended = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0x3A) { caps = !caps; return; }

    if (sc >= 128) return;
    char c = shift ? keymap_shift[sc] : keymap[sc];
    if (!c) return;

    if (caps && c >= 'a' && c <= 'z') c -= 32;
    else if (caps && c >= 'A' && c <= 'Z') c += 32;

    buf_push(c);
}

int keyboard_getchar(void) {
    if (head == tail) return -1;
    char c = buf[tail];
    tail = (tail + 1) % KB_BUF_SIZE;
    return (int)(unsigned char)c;
}
