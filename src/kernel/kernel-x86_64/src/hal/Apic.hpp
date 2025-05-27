#ifndef KERNEL_X86_64_APIC_HPP
#define KERNEL_X86_64_APIC_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    enum ApicMsrs : u64 {
        APIC_BASE = 0x1B,
        TPR = 0x80,
        SIV = 0xF0,
        ICRL = 0x300,
        ICRH = 0x310,
        LVT = 0x320,
        TDC = 0x3E0,
        TIC = 0x380,
        TCC = 0x390,
        DEADLINE = 0x6E0
    };

    enum Dest : u8 {
        NONE = 0b00,
        SELF = 0b01,
        ALL = 0b10,
        ALL_NO_SELF = 0b11
    };

    class Apic {
    public:
        Apic() = default;
        ~Apic() = default;

        bool isInitialized() const;

        void init(bool bootstrap);

        void calibrateTimer();

        void eoi();
        void ipi(u8 id, Dest dsh, u8 vector);
        void arm(usize ns, u8 vector);

        void setId(u32 apicId);
        u32 getId() const;

        void setIsX2Apic(bool val);
        bool getIsX2Apic() const;

    private:
        u32 toX2Apic(u32 reg);
        u32 read(u32 reg);
        void write(u32 reg, u32 data);

        u32 apicId {};

        u64 mmio {};
        u64 physMmio {};

        bool isX2Apic {};
        bool tscDeadline {};

        u64 p {};
        u64 n {};
        bool calibrated {};

        bool initialized {};
    };

    class IOApicManager {

    };

    class IOApic {

    };
}

#endif