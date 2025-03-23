.global loadGdtAsm
loadGdtAsm:
    lgdtq (%rdi)

.global reloadRegistersAsm
reloadRegistersAsm:
    push 0x08
    leaq .reloadCS(%rdi), %rax
    push %rax
    lretq
.reloadCS:
    movw %ax, 0x10
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret