.global updateTssAsm
updateTssAsm:
    mov ax, 0x28
    ltr ax
    ret