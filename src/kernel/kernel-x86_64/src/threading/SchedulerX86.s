.extern switchContextNewAsm

.global switchContextAsm
switchContextAsm:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp

    mov cr3, rdx

    mov rsp, [rsi]

    ret

.global switchContextMidAsm
switchContextMidAsm:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    sti

    ret

.global setStackAsm
setStackAsm:
    mov rax, rsp

    mov rsp, [rdi]

    push rsi

    push 0
    push 0
    push 0
    push 0
    push 0
    push rdx

    mov [rdi], rsp

    mov rsp, rax

    ret

.global threadTrampoline
threadTrampoline:
    mov ax, 0x18 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, rsp
    push 0x18 | 3
    push rax
    push 0x200
    push 0x20 | 3
    push r15

    mov r15, 0

    iret