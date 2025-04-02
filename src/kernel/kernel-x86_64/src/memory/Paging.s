.global initPaging
initPaging:
    // Disable paging
    mov ebx, cr0
    and ebx, ~(1 << 31)
    mov cr0, ebx

    // Enable PAE
    mov edx, cr4
    or  edx, (1 << 5)
    mov cr4, edx

    // Set LME (long mode enable)
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    // Replace 'pml4_table' with the appropriate physical address (and flags, if applicable)
    mov eax, [rdi]
    mov cr3, eax

    // Enable paging (and protected mode, if it isn't already active)
    or ebx, (1 << 31) | (1 << 0)
    mov cr0, ebx