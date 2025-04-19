#ifndef KERNEL_X86_64_CPU_HPP
#define KERNEL_X86_64_CPU_HPP

namespace kernel::x86_64::hal {
    class CpuManager {
    public:
        CpuManager() = default;
        ~CpuManager() = default;

        void init();

    private:
        void initSimd();
    };
}

#endif