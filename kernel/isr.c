#include "isr.h"
#include "kernel.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"

static const char *exception_names[] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FP", "Alignment Check", "Machine Check", "SIMD FP",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor", "VMM Communication", "Security", "Reserved"
};

static irq_handler_t irq_handlers[16] = {0};

void irq_register(uint8_t irq, irq_handler_t fn) {
    if (irq < 16) irq_handlers[irq] = fn;
}

static void irq_dispatch(uint8_t irq) {
    if (irq < 16 && irq_handlers[irq])
        irq_handlers[irq]();
    pic_send_eoi(irq);
}

void isr_handler(regs_t *r) {
    if (r->int_no >= 32 && r->int_no < 48) {
        irq_dispatch((uint8_t)(r->int_no - 32));
        return;
    }

    print("\n[EXCEPTION] ");
    if (r->int_no < 32)
        print(exception_names[r->int_no]);
    else
        print("Unknown");

    print("\n  int_no  = "); print_dec(r->int_no);
    print("\n  err_code= "); print_hex(r->err_code);
    print("\n  rip     = "); print_hex(r->rip);
    print("\n  cs      = "); print_hex(r->cs);
    print("\n  rflags  = "); print_hex(r->rflags);
    print("\n  rsp     = "); print_hex(r->rsp);
    print("\n  ss      = "); print_hex(r->ss);

    if (r->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        print("\n  cr2     = "); print_hex(cr2);
    }

    print("\nSystem halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
