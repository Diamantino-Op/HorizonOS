.global initPagingAsm
initPagingAsm:
    // Disable paging
    mov rbx, cr0
    and ebx, ~(1 << 31)
    mov cr0, rbx

    // Enable PAE
    mov rdx, cr4
    or  rdx, (1 << 5)
    mov cr4, rdx

    // Set LME (long mode enable)
    mov rcx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    // Replace 'pml4_table' with the appropriate physical address (and flags, if applicable)
    mov rax, [rdi]
    mov cr3, rax

    // Enable paging (and protected mode, if it isn't already active)
    or ebx, (1 << 31) | (1 << 0)
    mov cr0, rbx