#include "GDT.hpp"

namespace hal::x86_64 {
    void GdtManager::initGdt(Tss const& tss) {
        this->gdtInstance = Gdt();

        // Null Segment
        this->gdtInstance.entries[0] = GdtEntry();

        // Kernel Code Segment
        this->gdtInstance.entries[1] = GdtEntry(
            AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::EXECUTABLE,
            Flags::PAGE_GRANULARITY | Flags::LONG_MODE
        );

        // Kernel Data Segment
        this->gdtInstance.entries[2] = GdtEntry(
            AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE,
            Flags::PAGE_GRANULARITY | Flags::PROTECTED_SEGMENT
        );

        // User Code Segment
        this->gdtInstance.entries[3] = GdtEntry(
            AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::EXECUTABLE | AccessBytes::USER,
            Flags::PAGE_GRANULARITY | Flags::LONG_MODE
        );

        // User Data Segment
        this->gdtInstance.entries[4] = GdtEntry(
            AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::USER,
            Flags::PAGE_GRANULARITY | Flags::PROTECTED_SEGMENT
        );

        // TSS Segment
        this->gdtInstance.tssEntry = GdtTssEntry(tss);
    }

    void GdtManager::loadGdt() {
        this->gdtDescriptor = GdtDesc(this->gdtInstance);
    }

    Gdt GdtManager::getGdt() {
        return this->gdtInstance;
    }
}