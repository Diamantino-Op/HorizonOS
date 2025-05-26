#ifndef KERNEL_X86_64_HORIZONOS_ACPIPM_HPP
#define KERNEL_X86_64_HORIZONOS_ACPIPM_HPP

#include "Types.hpp"

#include "uacpi/types.h"
#include "uacpi/acpi.h"
#include "uacpi/io.h"

namespace kernel::x86_64::hal {
    class AcpiPM {
    public:
        AcpiPM() = default;
        ~AcpiPM() = default;

        void init();

        u64 read();

        bool supported();

        uacpi_interrupt_ret handleOverflow(uacpi_handle);

        static void calibrate(u64 ms);

        static u64 getNs();

    private:
        acpi_gas timerBlock {};
        uacpi_mapped_gas *timerBlockMapped {};

        u64 mask {};
        i64 offset {};
        u64 overflows {};
    };
}

#endif
