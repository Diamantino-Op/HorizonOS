#ifndef KERNEL_X86_64_ASM_HPP
#define KERNEL_X86_64_ASM_HPP

#include "Types.hpp"

namespace kernel::x86_64::utils {
    enum Msrs : u64 {
        EFER = 0xC0000080,
        STAR = 0xC0000081,
        LSTAR = 0xC0000082,
        CSTAR = 0xC0000083,
        FMASK = 0xC0000084,
        FSBAS = 0xC0000100,
        UGSBAS = 0xC0000101,
        KGSBAS = 0xc0000102,
    };

    struct __attribute__((packed)) Cr0Register {
        u8 protectedModeEnable : 1 {};
        u8 monitorCoProcessor : 1 {};
        u8 emulation : 1 {};
        u8 taskSwitched : 1 {};
        u8 extensionType : 1 {};
        u8 numericError : 1 {};
        u16 reserved1 : 10 {};
        u8 writeProtect : 1 {};
        u8 reserved2 : 1 {};
        u8 alignmentMask : 1 {};
        u16 reserved3 : 10 {};
        u8 notWriteTrough : 1 {};
        u8 cacheDisable : 1 {};
        u8 paging : 1 {};
        u32 reserved4 {};
    };

    union Cr0RegisterU {
        Cr0Register reg {};
        u64 value;
    };

    struct __attribute__((packed)) Cr4Register {
        u8 virtual8086Ext : 1 {};
        u8 protectedVirtModeInts : 1 {};
        u8 timeStampOnlyR0 : 1 {};
        u8 debugExtensions : 1 {};
        u8 pageSizeExtension : 1 {};
        u8 physicalAddressExtension : 1 {};
        u8 machineCheckException : 1 {};
        u8 pageGlobalEnable : 1 {};
        u8 perfMonitorCounterEnable : 1 {};
        u8 fxsaveFxrstorSupport : 1 {};
        u8 unmaskedSimdExceptionsSupport : 1 {};
        u8 userModeInstPrevention : 1 {};
        u8 level5Paging : 1 {};
        u8 virtMachineExtEnable : 1 {};
        u8 saferModeExtEnable : 1 {};
        u8 reserved1 : 1 {};
        u8 enableSbaseInstructions : 1 {};
        u8 pcidEnable : 1 {};
        u8 xsaveExtendedEnable : 1 {};
        u8 reserved2 : 1 {};
        u8 supervisorExecutionProtEnable : 1 {};
        u8 supervisorAccessProtEnable : 1 {};
        u8 userModePageProtKeysEnable : 1 {};
        u8 controlFlowTechEnable : 1 {};
        u8 supervisorModePageProtKeysEnable : 1 {};
        u64 reserved3 : 39 {};
    };

    union Cr4RegisterU {
        Cr4Register reg {};
        u64 value;
    };

    struct __attribute__((packed)) Cr8Register {
        u8 priority : 4;
        u64 reserved : 60;
    };

    union Cr8RegisterU {
        Cr8Register reg {};
        u64 value;
    };

    struct __attribute__((packed)) XCr0Register {
        u8 xsaveSaveX87 : 1 {};
        u8 xsaveSaveSSE : 1 {};
        u8 avxEnable : 1 {};
        u8 bndregEnable : 1 {};
        u8 bndcsrEnable : 1 {};
        u8 avx512Enable : 1 {};
        u8 zmm0_15Enable : 1 {};
        u8 zmm16_31Enable : 1 {};
        u8 pkruEnable : 1 {};
        u64 reserved : 55;
    };

    union XCr0RegisterU {
        XCr0Register reg {};
        u64 value;
    };

    class Asm {
    public:
        static void cli();
        static void sti();

        static void hlt();
        [[noreturn]] static void lhlt();
        static void pause();

        static void invalidatePage(u64 addr);

        // CRs

        static u64 readCr0();
        static void writeCr0(u64 value);

        static u64 readCr2();

        static u64 readCr3();
        static void writeCr3(u64 value);

        static u64 readCr4();
        static void writeCr4(u64 value);

        static u64 readCr8();
        static void writeCr8(u64 value);

        // AVX / SSE

        static u64 readXCr(u32 i);
        static void writeXCr(u32 i, u64 value);

        static void xsave(const u64 *region);
        static void xrstor(const u64 *region);
        static void fninit();
        static void fxsave(const u64 *region);
        static void fxrstor(const u64 *region);

        // Msrs

        static u64 rdmsr(u64 msr);
        static void wrmsr(u64 msr, u64 value);
    };
}

#endif