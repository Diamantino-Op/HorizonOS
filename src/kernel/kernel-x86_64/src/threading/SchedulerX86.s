.global switchContextAsm
switchContextAsm:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi + 16], rsp

    mov rsp, [rsi + 16]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret