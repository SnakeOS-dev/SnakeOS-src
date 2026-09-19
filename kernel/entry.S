MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 4096
pml4:   resq 512
pdpt:   resq 512
pd:     resq 512

align 16
stack_bottom:
    resb 16384
stack_top:

section .data
align 8
gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt_end:

gdt_ptr:
    dw gdt_end - gdt - 1
    dq gdt

align 16
idt:
    times 512 dq 0
idt_end:

idt_ptr:
    dw idt_end - idt - 1
    dq idt

section .text
bits 32
global _start
extern kernel_main

_start:
    mov esp, stack_top
    mov edi, ebx
    mov esi, eax

    mov eax, pml4
    mov cr3, eax

    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax

    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax

    mov ecx, 0
.map_pd:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83
    mov [pd + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jne .map_pd

    lgdt [gdt_ptr]

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    jmp 0x08:.long_mode

bits 64
.long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    lidt [idt_ptr]

    mov rdi, rsi
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
