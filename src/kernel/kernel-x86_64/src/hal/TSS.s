.section .text

.global updateTssAsm
updateTssAsm:
    mov ax, 0x30
    ltr ax
    ret