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
#include "blkdev.h"
#include "ata.h"
#include "ramdisk.h"
#include "vfs.h"
#include "ext2.h"

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
    print("  lsdev         list block devices\n");
    print("  mount <dev> <path> <fstype>  mount filesystem\n");
    print("  umount <path>                unmount filesystem\n");
    print("  ls <path>                    list directory\n");
    print("  cat <path>                   print file\n");
    print("  write <path> <text>          write to file\n");
    print("  mkdir <path>                 create directory\n");
    print("  touch <path>                 create file\n");
    print("  rm <path>                    remove file\n");
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

static void cmd_lsdev(void) {
    for (blockdev_t *d = blockdev_first(); d; d = blockdev_next(d)) {
        print(d->name);
        print("  sectors="); print_dec(d->sector_count);
        print("  ("); print_dec(d->sector_count * d->sector_size / 1024);
        print(" KiB)\n");
    }
}

static void cmd_mount(const char *arg) {
    char dev[32], path[64], fstype[16];
    int i = 0, j = 0, k = 0;
    while (*arg == ' ') arg++;
    while (arg[i] && arg[i] != ' ' && i < 31) { dev[i] = arg[i]; i++; }
    dev[i] = 0;
    while (arg[i] == ' ') i++;
    while (arg[i] && arg[i] != ' ' && j < 63) { path[j] = arg[i]; i++; j++; }
    path[j] = 0;
    while (arg[i] == ' ') i++;
    while (arg[i] && arg[i] != ' ' && k < 15) { fstype[k] = arg[i]; i++; k++; }
    fstype[k] = 0;
    if (!dev[0] || !path[0] || !fstype[0]) {
        print("usage: mount <dev> <path> <fstype>\n");
        return;
    }
    int r = vfs_mount(dev, path, fstype);
    if (r == VFS_OK) print("mounted\n");
    else { print("mount failed: "); print_dec((uint64_t)(-r)); print("\n"); }
}

static void cmd_umount(const char *arg) {
    while (*arg == ' ') arg++;
    int r = vfs_unmount(arg);
    if (r == VFS_OK) print("unmounted\n");
    else { print("umount failed: "); print_dec((uint64_t)(-r)); print("\n"); }
}

static void cmd_ls(const char *arg) {
    while (*arg == ' ') arg++;
    if (!*arg) arg = "/";
    vfs_node_t *dir = vfs_lookup(arg);
    if (!dir) { print("not found\n"); return; }
    if (!(dir->flags & VFS_DIR)) { print("not a directory\n"); return; }
    uint64_t i = 0;
    vfs_node_t *ent;
    while ((ent = vfs_readdir(dir, i++)) != 0) {
        if (ent->flags & VFS_DIR) print("d ");
        else print("- ");
        print(ent->name);
        print("  ");
        print_dec(vfs_size(ent));
        print("\n");
        vfs_node_destroy(ent);
    }
}

static void cmd_cat(const char *arg) {
    while (*arg == ' ') arg++;
    vfs_node_t *n = vfs_lookup(arg);
    if (!n) { print("not found\n"); return; }
    if (n->flags & VFS_DIR) { print("is a directory\n"); return; }
    uint64_t sz = vfs_size(n);
    char *buf = (char *)kmalloc(sz + 1);
    if (!buf) { print("oom\n"); return; }
    int64_t r = vfs_read(n, buf, 0, sz);
    if (r < 0) { print("read error\n"); kfree(buf); return; }
    for (int64_t i = 0; i < r; i++) putchar(buf[i]);
    print("\n");
    kfree(buf);
}

static void cmd_write(const char *arg) {
    char path[64];
    int i = 0;
    while (*arg == ' ') arg++;
    while (arg[i] && arg[i] != ' ' && i < 63) { path[i] = arg[i]; i++; }
    path[i] = 0;
    while (arg[i] == ' ') i++;
    const char *text = arg + i;
    vfs_node_t *n = vfs_lookup(path);
    if (!n) {
        vfs_node_t *parent = vfs_lookup("/");
        char *p = path;
        char *last = p;
        while (*p) { if (*p == '/') last = p + 1; p++; }
        char dirname[64];
        int di = 0;
        p = path;
        while (p < last - 1 && di < 63) { dirname[di++] = *p++; }
        dirname[di] = 0;
        if (di > 0) parent = vfs_lookup(dirname);
        if (!parent) { print("parent not found\n"); return; }
        if (parent->ops && parent->ops->create) {
            if (parent->ops->create(parent, last, VFS_FILE) != VFS_OK) {
                print("create failed\n");
                return;
            }
        }
        n = vfs_lookup(path);
        if (!n) { print("create failed\n"); return; }
    }
    if (n->flags & VFS_DIR) { print("is a directory\n"); return; }
    uint64_t len = 0;
    while (text[len]) len++;
    int64_t r = vfs_write(n, text, 0, len);
    if (r < 0) { print("write error\n"); return; }
    print("wrote "); print_dec((uint64_t)r); print(" bytes\n");
}

static void cmd_mkdir(const char *arg) {
    while (*arg == ' ') arg++;
    vfs_node_t *n = vfs_mkdir_p(arg);
    if (!n) print("mkdir failed\n");
}

static void cmd_touch(const char *arg) {
    while (*arg == ' ') arg++;
    vfs_node_t *n = vfs_lookup(arg);
    if (n) return;
    vfs_node_t *parent = vfs_lookup("/");
    char *p = (char *)arg;
    char *last = p;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    char dirname[64];
    int di = 0;
    p = (char *)arg;
    while (p < last - 1 && di < 63) { dirname[di++] = *p++; }
    dirname[di] = 0;
    if (di > 0) parent = vfs_lookup(dirname);
    if (!parent) { print("parent not found\n"); return; }
    if (parent->ops && parent->ops->create) {
        if (parent->ops->create(parent, last, VFS_FILE) != VFS_OK)
            print("create failed\n");
    }
}

static void cmd_rm(const char *arg) {
    while (*arg == ' ') arg++;
    vfs_node_t *n = vfs_lookup(arg);
    if (!n) { print("not found\n"); return; }
    if (n->flags & VFS_MOUNT) { print("is a mount point\n"); return; }
    int r = vfs_remove(n);
    if (r == VFS_OK) print("removed\n");
    else { print("remove failed: "); print_dec((uint64_t)(-r)); print("\n"); }
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
    else if (str_eq(p, "lsdev"))      cmd_lsdev();
    else if (str_prefix(p, "mount ")) cmd_mount(p + 6);
    else if (str_prefix(p, "umount ")) cmd_umount(p + 7);
    else if (str_prefix(p, "ls "))    cmd_ls(p + 3);
    else if (str_prefix(p, "cat "))   cmd_cat(p + 4);
    else if (str_prefix(p, "write ")) cmd_write(p + 6);
    else if (str_prefix(p, "mkdir ")) cmd_mkdir(p + 6);
    else if (str_prefix(p, "touch ")) cmd_touch(p + 6);
    else if (str_prefix(p, "rm "))    cmd_rm(p + 3);
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

    ata_init();
    vfs_init();
    ext2_register();
    vfs_dev_populate();

    print("SnakeOS v0.00\n");
    print("type 'help' for commands\n");

    __asm__ volatile ("sti");
    shell_loop();
}
