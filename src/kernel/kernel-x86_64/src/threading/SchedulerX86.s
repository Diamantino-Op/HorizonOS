.extern scheduleEntry
.extern getCurrThreadRsp
.extern loadNewThread

.global switchContextAsm
switchContextAsm:
    cli
    cld

    call getCurrThreadRsp

    cmp rax, 0
    je ctxSwitchFinish

    pop rcx
    pop r8
    pop r9
    pop r10
    pop r11

    mov rsp, rax

    push r11
    push r10
    push r9
    push r8
    push rcx

    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp

    call scheduleEntry

    mov cr3, rdx

    mov rsp, rax

    call loadNewThread

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

ctxSwitchFinish:
    sti

    iretq

.global setStackAsm
setStackAsm:
    mov rax, rsp

    mov rsp, [rdi]

    push 0x10
    push [rdi]
    push 0x292
    push 0x08
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
    mov ax, 0x20 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, r14
    push 0x20 | 3
    push rax
    push 0x200
    push 0x18 | 3
    push r15

    mov r14, 0
    mov r15, 0

    iret

.global threadTrampoline64
threadTrampoline64:
    mov ax, 0x20 | 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, r14
    push 0x20 | 3
    push rax
    push 0x200
    push 0x28 | 3
    push r15

    mov r14, 0
    mov r15, 0

    iret