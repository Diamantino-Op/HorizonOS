#ifndef KERNEL_X86_64_INTERRUPTS_HPP
#define KERNEL_X86_64_INTERRUPTS_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    using IsrHandler = void(*)();

    struct __attribute__((packed)) ExceptionFrame {
        u64 r15;
        u64 r14;
        u64 r13;
        u64 r12;
        u64 r11;
        u64 r10;
        u64 r9;
        u64 r8;
        u64 rbp;
        u64 rdi;
        u64 rsi;
        u64 rdx;
        u64 rcx;
        u64 rbx;
        u64 rax;

        u64 intNo;
        u64 errNo;

        u64 rip;
        u64 cs;
        u64 rFlags;
        u64 rsp;
        u64 ss;
    };

    struct __attribute__((packed)) InterruptFrame {
        u64 intNo;

        u64 rip;
        u64 cs;
        u64 rFlags;
        u64 rsp;
        u64 ss;
    };

    struct __attribute__((packed)) NMIFrame {
        u64 intNo;

        u64 rip;
        u64 cs;
        u64 rFlags;
        u64 rsp;
        u64 ss;
    };

    static char const* faultMessages[32] = {
        "Division By Zero",
        "Debug",
        "Non Maskable Interrupt",
        "Breakpoint",
        "Detected Overflow",
        "Out Of Bounds",
        "Invalid OpCode",
        "No CoProcessor",
        "Double Fault",
        "CoProcessor Segment Overrun",
        "Bad Tss",
        "Segment Not Present",
        "Stack Fault",
        "General Protection Fault",
        "Page Fault",
        "Unknown Interrupt",
        "CoProcessor Fault",
        "Alignment Check",
        "Machine Check",
        "Simd Floating Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
        "Reserved",
        "Hypervisor Injection Exception",
        "Vmm Communication Exception",
        "Security Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
    };

    class Interrupts {
    public:
        static void handleInterrupt(const InterruptFrame &frame);
        static void handleException(const ExceptionFrame &frame);

        static void handleNonMaskableInt(const NMIFrame &frame);

        static void handlePageFault(const ExceptionFrame &frame);

        static void kernelPanic(const ExceptionFrame &frame);
        static void userPanic(const ExceptionFrame &frame);

        static void backtrace(usize rbp);

        static void setHandler(u8 id, u64 *handler);

        static void mask(u8 id);
        static void unmask(u8 id);

    private:
        static IsrHandler handlers[224];
    };

    extern "C" uPtr interruptTable[256];

    extern "C" void handleInterruptAsm(usize stackFrame);
    extern "C" void handleNonMaskableInt(usize stackFrame);
    extern "C" void handleExceptionAsm(usize stackFrame);
}

#endif