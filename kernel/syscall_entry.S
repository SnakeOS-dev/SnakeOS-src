section .text
bits 64
extern syscall_handler
extern sched_switch_stack
extern g_user_rsp
extern g_kernel_rsp
global syscall_entry
syscall_entry:
mov [rel g_user_rsp], rsp
mov rsp, [rel g_kernel_rsp]
push qword 0x23
push qword [rel g_user_rsp]
push r11
push qword 0x2B
push rcx
push qword 0
push qword 0x100
push rax
push rbx
push rcx
push rdx
push rbp
push rsi
push rdi
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15
cld
mov rdi, rsp
call syscall_handler
mov rdi, rsp
call sched_switch_stack
test rax, rax
jz .resume
mov rsp, rax
.resume:
pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rdi
pop rsi
pop rbp
pop rdx
pop rcx
pop rbx
pop rax
add rsp, 16
iretq
