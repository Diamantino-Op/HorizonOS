.extern switchContextMidAsm

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

    mov rdi, rcx

    call switchContextMidAsm

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

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
    push rcx
    push rdx

    mov [rdi], rsp

    mov rsp, rax

    ret

.global threadTrampoline32
threadTrampoline32:
    mov ax, 0x18 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, r14
    push 0x18 | 3
    push rax
    push 0x200
    push 0x20 | 3
    push r15

    mov r14, 0
    mov r15, 0

    iret

.global threadTrampoline64
threadTrampoline64:
    mov ax, 0x28 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, r14
    push 0x28 | 3
    push rax
    push 0x200
    push 0x20 | 3
    push r15

    mov r14, 0
    mov r15, 0

    iret