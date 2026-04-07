.extern scheduleEntry
.extern loadNewThread
.extern finishScheduleSwitch
.extern checkDisabled

.global switchContextAsm
switchContextAsm:
    call checkDisabled

    cmp rax, 1
    je enDisabled

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

enDisabled:
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
