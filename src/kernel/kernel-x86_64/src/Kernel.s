.extern kernelStart
.global _start
_start:
    mov rdi, rsp

    call kernelStart