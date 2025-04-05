.global initPagingAsm
initPagingAsm:
    // Replace 'pml4_table' with the appropriate physical address (and flags, if applicable)
    mov cr3, [rdi]