#ifndef KERNEL_X86_64_APIC_HPP
#define KERNEL_X86_64_APIC_HPP

#include "Types.hpp"

#include "uacpi/acpi.h"

namespace kernel::x86_64::hal {
    enum ApicMsrs : u64 {
        LAPIC_ID = 0x20, // Local APIC ID
        LAPIC_VER = 0x30, // Local APIC Version
        LAPIC_TPR = 0x80, // Task Priority
        LAPIC_APR = 0x90, // Arbitration Priority
        LAPIC_BASE = 0x1B, // Local APIC Base
        LAPIC_PPR = 0xA0, // Processor Priority
        LAPIC_EOI = 0xB0, // EOI
        LAPIC_RRD = 0xC0, // Remote Read
        LAPIC_LDR = 0xD0, // Logical Destination
        LAPIC_DFR = 0xE0, // Destination Format
        LAPIC_SIV = 0xF0, // Spurious Interrupt Vector
        LAPIC_ISR = 0x100, // In-Service (8 registers)
        LAPIC_TMR = 0x180, // Trigger Mode (8 registers)
        LAPIC_IRR = 0x200, // Interrupt Request (8 registers)
        LAPIC_ESR = 0x280, // Error Status
        LAPIC_ICRL = 0x300, // Interrupt Command (Low)
        LAPIC_ICRH = 0x310, // Interrupt Command (High)
        LAPIC_LVT = 0x320, // LVT Timer Initial Count
        LAPIC_THERMAL = 0x330, // LVT Thermal Sensor
        LAPIC_PERF = 0x340, // LVT Performance Counter
        LAPIC_LINT0 = 0x350, // LVT LINT0
        LAPIC_LINT1 = 0x360, // LVT LINT1
        LAPIC_ERROR = 0x370, // LVT Error
        LAPIC_TIC = 0x380, // Initial Count (for Timer)
        LAPIC_TCC = 0x390, // Current Count (for Timer)
        LAPIC_TDC = 0x3E0, // Divide Configuration (for Timer)
        LAPIC_DEADLINE = 0x6E0 // TSC Deadline Mode
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
        void arm(u64 ns, u8 vector, bool periodic);

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

        void init(u64 physMmio, u32 gsiBase);

        void setIdx(u64 idx, u8 vector, u64 dest, u16 flags, IOApicDelivery delivery);

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
    };

    class IOApicManager {
    public:
        IOApicManager() = default;
        ~IOApicManager() = default;

        void init();

        void setGsi(u64 gsi, u8 vector, u64 dest, u16 flags, IOApicDelivery delivery) const;

        void maskGsi(u32 gsi) const;
        void unmaskGsi(u32 gsi) const;

        void mask(u8 vector);
        void unmask(u8 vector);

        IOApic *gsiToIOApic(u32 gsi) const;

        u32 irqToIso(u8 irq);

        bool isInitialized() const;

        u8 getMaxRange() const;

    private:
        u8 maxRange {};

        IOApic *ioApics {};

        bool initialized {};
    };
}

#endif