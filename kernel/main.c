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
#include "gdt.h"
#include "sched.h"
#include "task.h"
#include "syscall.h"
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
static volatile uint64_t g_timer_ticks = 0;
static void timer_tick(void) {
pit_tick();
sched_need_resched();
g_timer_ticks++;
if ((g_timer_ticks & 0x3F) == 0) putchar('.');
}
static void kb_irq(void) { keyboard_handle(); }
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
print("  sched         scheduler debug stats\n");
print("  tasks         list tasks\n");
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
static void cmd_sched(void) {
sched_dump_stats();
}
static void cmd_tasks(void) {
task_t *t = g_current;
if (!t) { print("no current task\n"); return; }
task_t *start = t;
int i = 0;
do {
print("["); print_dec(i++); print("] pid=");
print_dec(t->pid);
print(" state="); print_dec(t->state);
print(" rsp=");   print_hex(t->rsp);
print(" ustk=");  print_hex(t->ustk);
if (t == g_current) print(" <current");
print("\n");
t = t->next;
} while (t && t != start);
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
else if (str_eq(p, "sched"))      cmd_sched();
else if (str_eq(p, "tasks"))      cmd_tasks();
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
static const char msg_a[] = "task A\n";
static const char msg_b[] = "task B\n";
static void user_task_a(void) {
uint64_t r;
for (;;) {
__asm__ volatile (
"syscall"
: "=a"(r)
: "a"(1UL), "D"(1UL), "S"(msg_a), "d"(7UL)
: "rcx", "r11", "memory"
);
__asm__ volatile (
"syscall"
: "=a"(r)
: "a"(24UL)
: "rcx", "r11", "memory"
);
}
}
static void user_task_b(void) {
uint64_t r;
for (;;) {
__asm__ volatile (
"syscall"
: "=a"(r)
: "a"(1UL), "D"(1UL), "S"(msg_b), "d"(7UL)
: "rcx", "r11", "memory"
);
__asm__ volatile (
"syscall"
: "=a"(r)
: "a"(24UL)
: "rcx", "r11", "memory"
);
}
}
static void shell_loop(void) {
shell_prompt();
for (;;) {
int c = keyboard_getchar();
if (c < 0) { __asm__ volatile ("hlt"); continue; }
shell_input(c);
}
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
gdt_init();
sched_init();
syscall_init();
static task_t shell;
shell.pid = 0;
shell.state = TASK_RUNNING;
shell.next = 0;
shell.rsp = 0;
shell.ustk = 0;
shell.brk = 0;
shell.brk_start = 0;
shell.kstack = (uint8_t *)kmalloc(TASK_KSTACK_SIZE);
sched_add(&shell);
print("creating tasks...\n");
uint64_t ua = task_alloc_stack();
task_t *ta = task_create((uint64_t)user_task_a, ua + TASK_USTACK_SIZE - 16);
if (ta) sched_add(ta);
uint64_t ub = task_alloc_stack();
task_t *tb = task_create((uint64_t)user_task_b, ub + TASK_USTACK_SIZE - 16);
if (tb) sched_add(tb);
print("ta=");      print_hex((uint64_t)ta);
print(" ta->rsp="); print_hex(ta ? ta->rsp : 0);
print(" ta->kstk="); print_hex(ta ? (uint64_t)ta->kstack : 0);
print("\n");
print("tb=");      print_hex((uint64_t)tb);
print(" tb->rsp="); print_hex(tb ? tb->rsp : 0);
print(" tb->kstk="); print_hex(tb ? (uint64_t)tb->kstack : 0);
print("\n");
print("g_current="); print_hex((uint64_t)g_current);
print(" g_kernel_rsp="); print_hex(g_kernel_rsp);
print("\n");
print("SnakeOS v0.00\n");
print("type 'help' for commands\n");
__asm__ volatile ("sti");
shell_loop();
}
