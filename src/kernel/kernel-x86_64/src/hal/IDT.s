section .text

.global loadIdtAsm
loadIdtAsm:
    lidt [rdi]
    ret