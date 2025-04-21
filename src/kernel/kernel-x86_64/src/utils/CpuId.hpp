#ifndef KERNEL_X86_64_CPUID_HPP
#define KERNEL_X86_64_CPUID_HPP

#include "Types.hpp"

namespace kernel::x86_64::utils {
    struct CpuIdRegs {
        u32 eax;
        u32 ebx;
        u32 ecx;
        u32 edx;
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
        static CpuIdRegs getCpuIdRegs(u32 leaf = 0, u32 subLeaf = 0);

    private:
        u32 els[4] {};
        char str[16] {};
    };
}

#endif