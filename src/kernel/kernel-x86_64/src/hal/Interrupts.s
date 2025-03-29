.section .text

.extern handleInterruptAsm

interruptCommon:
    cld

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call handleInterruptAsm

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16

    iretq

.macro createInterruptHandler interruptNumber
interruptHandler\interruptNumber:
    .if \interruptNumber != 8 && \interruptNumber != 10 && \interruptNumber != 11 && \interruptNumber != 12 && \interruptNumber != 13 && \interruptNumber != 14 && \interruptNumber != 17 && \interruptNumber != 30
        push 0
    .endif

    push \interruptNumber
    jmp interruptCommon
.endm

.altmacro
.set i, 0
.rept 256
    createInterruptHandler %i
    .set i, i + 1
.endr

.macro label i
    .quad interruptHandler\i
.endm


.section .data

.global interruptTable

interruptTable:
.set i, 0
.rept 256
    label %i
    .set i, i + 1
.endr
