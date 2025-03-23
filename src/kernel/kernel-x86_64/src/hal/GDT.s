global loadGdtAsm
loadGdtAsm:
    lgdt [rdi]

global reloadRegistersAsm
reloadRegistersAsm:
    push 0x08
    lea rax, [rip + .reload_CS]
    push rax
    lretq
.reloadCS:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret