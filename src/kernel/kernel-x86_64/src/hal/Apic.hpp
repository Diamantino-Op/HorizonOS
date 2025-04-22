#ifndef KERNEL_X86_64_APIC_HPP
#define KERNEL_X86_64_APIC_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    class Apic {
    public:
        Apic() = default;
        ~Apic() = default;

        void init();

    private:
        u32 apicId {};
    };
}

#endif