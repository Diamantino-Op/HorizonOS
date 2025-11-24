.extern kernelMain
.global _start
_start:
    mov rdi, rsp

    call kernelMain