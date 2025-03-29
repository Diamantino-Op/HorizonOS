.global loadIdtAsm
loadIdtAsm:
    lidt [rdi]
    ret