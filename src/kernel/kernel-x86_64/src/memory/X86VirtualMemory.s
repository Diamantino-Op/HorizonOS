.global loadPageTableAsm
loadPageTableAsm:
     mov rax, rdi
     mov cr3, rax
     ret