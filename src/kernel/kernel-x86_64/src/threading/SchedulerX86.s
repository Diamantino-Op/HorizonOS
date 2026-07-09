.extern scheduleEntry
.extern loadNewThread
.extern finishScheduleSwitch
.extern checkDisabled
.extern prepareScheduleSwitch
.extern kernelThreadReturned

.global switchContextAsm
switchContextAsm:
    sub rsp, 8
    call checkDisabled
    add rsp, 8

    cmp rax, 1
    je enDisabled

    sub rsp, 8
    call prepareScheduleSwitch
    add rsp, 8

    # Saved with the outgoing stack and restored only when that same
    # continuation is scheduled again.
    push rax

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

    call finishScheduleSwitch

    pop rax
    test al, al
    jz enDisabled
    sti

enDisabled:
    ret

.global setStackAsm
setStackAsm:
    mov rax, rsp

    mov rsp, [rdi]

    push rsi

    # Initial contexts enter their trampoline with interrupts disabled.
    # User contexts enable them through IRET; kernelThreadTrampoline does
    # so explicitly.
    push 0

    push 0
    push 0
    push 0
    push 0
    # SysV args: rdx=usermode entry RIP, rcx=user stack.
    # switchContextAsm pops r15 then r14, and trampoline uses r15 as RIP, r14 as user RSP.
    push rcx
    push rdx

    mov [rdi], rsp

    mov rsp, rax

    ret

.global setUserStackAsm
setUserStackAsm:
    mov rax, rsp

    mov rsp, [rdi]

    # Main Fun Args
    push 0
    push 0
    push 0
    push 0

    mov [rdi], rsp

    mov rsp, rax

    ret

.global kernelThreadTrampoline
kernelThreadTrampoline:
    sti
    call r15
    call kernelThreadReturned

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

    swapgs

    iretq

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

    swapgs

    iretq
