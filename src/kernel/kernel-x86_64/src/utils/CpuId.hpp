#ifndef KERNEL_X86_64_CPUID_HPP
#define KERNEL_X86_64_CPUID_HPP

#include "Types.hpp"

namespace kernel::x86_64::utils {
    struct CpuIdResult {
        u32 eax {};
        u32 ebx {};
        u32 ecx {};
        u32 edx {};
    };

    union Brand {
        char value[48] {};
        u32 regs[12];
    };

    union Vendor {
        char value[12] {};
        u32 regs[3];
    };

    class CpuId {
    public:
        static CpuIdResult get(u32 leaf, u32 subLeaf);

        static char *getVendor();
        static char *getBrand();

        static bool hasXSave();
        static bool hasAvx();
        static bool hasAvx512();

        static u32 getXSaveSize();
    };
}

#endif