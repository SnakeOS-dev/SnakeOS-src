section .text
bits 64

global gdt_flush
gdt_flush:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

global tss_flush
tss_flush:
    mov ax, 0x30
    ltr ax
    ret
