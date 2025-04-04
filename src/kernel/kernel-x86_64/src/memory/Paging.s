.global initPagingAsm
initPagingAsm:
    // Replace 'pml4_table' with the appropriate physical address (and flags, if applicable)
    mov rax, [rdi]
    mov cr3, rax