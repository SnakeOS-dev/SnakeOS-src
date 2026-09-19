#include "kernel.h"
#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "pmm.h"
#include "isr.h"
#include "vmm.h"
#include "heap.h"

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

static void timer_tick(void) { pit_tick(); }
static void kb_irq(void)     { keyboard_handle(); }

#define CMD_BUF_SIZE 128

static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_len = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int str_prefix(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

static void cmd_help(void) {
    print("Commands:\n");
    print("  help          show this\n");
    print("  clear         clear screen\n");
    print("  version       kernel version\n");
    print("  uptime        ticks since boot\n");
    print("  mem           pmm stats\n");
    print("  heap          heap stats\n");
    print("  alloc <n>     kmalloc n bytes\n");
    print("  echo <text>   print text\n");
    print("  reboot        reboot machine\n");
}

static void cmd_version(void) {
    print("SnakeOS v0.00\n");
    print("built " __DATE__ " " __TIME__ "\n");
}

static void cmd_uptime(void) {
    print("ticks: "); print_dec(pit_ticks()); print("\n");
}

static void cmd_mem(void) {
    print("pmm total: "); print_dec(pmm_total_pages());
    print(" free: ");     print_dec(pmm_free_pages());
    print("\n");
}

static void cmd_heap(void) {
    size_t u, f, t;
    heap_stats(&u, &f, &t);
    print("used: ");  print_dec(u);
    print(" free: "); print_dec(f);
    print(" total: "); print_dec(t);
    print("\n");
}

static void cmd_alloc(const char *arg) {
    uint64_t n = 0;
    while (*arg == ' ') arg++;
    if (!*arg) { print("usage: alloc <bytes>\n"); return; }
    while (*arg >= '0' && *arg <= '9') {
        n = n * 10 + (*arg - '0');
        arg++;
    }
    if (n == 0) { print("invalid size\n"); return; }
    void *p = kmalloc((size_t)n);
    if (!p) { print("alloc failed\n"); return; }
    print("allocated "); print_dec(n);
    print(" bytes at "); print_hex((uint64_t)p); print("\n");
}

static void cmd_echo(const char *arg) {
    while (*arg == ' ') arg++;
    print(arg);
    print("\n");
}

static void cmd_reboot(void) {
    print("rebooting...\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void run_command(void) {
    cmd_buf[cmd_len] = 0;

    const char *p = cmd_buf;
    while (*p == ' ') p++;

    if (*p == 0) return;

    if (str_eq(p, "help"))            cmd_help();
    else if (str_eq(p, "clear"))      vga_clear();
    else if (str_eq(p, "version"))    cmd_version();
    else if (str_eq(p, "uptime"))     cmd_uptime();
    else if (str_eq(p, "mem"))        cmd_mem();
    else if (str_eq(p, "heap"))       cmd_heap();
    else if (str_eq(p, "reboot"))     cmd_reboot();
    else if (str_prefix(p, "alloc ")) cmd_alloc(p + 6);
    else if (str_prefix(p, "echo "))  cmd_echo(p + 5);
    else {
        print("unknown command: ");
        print(p);
        print("\n");
    }
}

static void shell_prompt(void) {
    print("snake> ");
}

static void shell_input(int c) {
    if (c == '\n') {
        putchar('\n');
        run_command();
        cmd_len = 0;
        shell_prompt();
        return;
    }
    if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            putchar('\b');
        }
        return;
    }
    if (c < 32) return;
    if (cmd_len >= CMD_BUF_SIZE - 1) return;

    cmd_buf[cmd_len++] = (char)c;
    putchar((char)c);
}

void kernel_main(uint32_t mbi_addr) {
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

    pmm_init(mbi_addr);
    vmm_init();
    heap_init();

    print("SnakeOS v0.00\n");
    print("type 'help' for commands\n\n");
    shell_prompt();

    __asm__ volatile ("sti");

    for (;;) {
        int c = keyboard_getchar();
        if (c < 0) { __asm__ volatile ("hlt"); continue; }
        shell_input(c);
    }
}
