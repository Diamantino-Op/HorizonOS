#ifndef KERNEL_X86_64_APIC_HPP
#define KERNEL_X86_64_APIC_HPP

#include "Types.hpp"

#include "uacpi/acpi.h"

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

    enum IOApicDelivery : u32 {
        FIXED = (0b000 << 8),
        LOW_PRIORITY = (0b001 << 8),
        SMI = (0b010 << 8),
        NMI = (0b100 << 8),
        INIT = (0b101 << 8),
        EXT_INT = (0b111 << 8),
    };

    enum IOApicFlags : u32 {
        MASKED = (1 << 16),
        LEVEL_SENSITIVE = (1 << 15),
        ACTIVE_LOW = (1 << 13),
        LOGICAL = (1 << 11),
    };

    class Apic {
    public:
        Apic() = default;
        ~Apic() = default;

        bool isInitialized() const;

        void init();

        void calibrateTimer();

        void eoi();
        void ipi(u8 id, Dest dsh, u8 vector);
        void arm(u64 ns, u8 vector);

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

    class IOApic {
    public:
        IOApic() = default;
        ~IOApic() = default;

        bool isInitialized() const;

        void init(u64 physMmio, u32 gsiBase);

        void setIdx(u64 idx, u8 vector, u64 dest, IOApicFlags flags, IOApicDelivery delivery);

        void maskIdx(u64 idx);
        void unmaskIdx(u64 idx);

        Pair getGsiRange() const;

    private:
        u32 entry(u32 idx);

        u32 read(u32 reg) const;
        void write(u32 reg, u32 data) const;

        u64 readEntry(u32 idx);
        void writeEntry(u32 idx, u64 data);

        u64 mmio {};
        u32 gsiBase {};
        u64 redirects {};

        bool initialized {};
    };

    class IOApicManager {
    public:
        IOApicManager() = default;
        ~IOApicManager() = default;

        void setGsi(u64 gsi, u8 vector, u64 dest, IOApicFlags flags, IOApicDelivery delivery);

        void maskGsi(u32 gsi);
        void unmaskGsi(u32 gsi);

        void mask(u8 vector);
        void unmask(u8 vector);

        IOApic &gsiToIOApic(u32 gsi);

        u32 irqToIso(u8 irq);

    private:
        IOApic *ioApics {};
    };
}

#endif