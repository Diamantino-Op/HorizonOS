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

    struct Branding {
        char vendor[12] {};
        char brand[48] {};

        char *getVendor() {
            return vendor;
        }

        char *getBrand() {
            return brand;
        }
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