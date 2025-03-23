.global updateTssAsm
updateTssAsm:
    movw %ax, 0x28
    ltr %ax
    ret