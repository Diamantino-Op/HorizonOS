#ifndef KERNEL_COMMON_HORIZONOS_ACPIPM_HPP
#define KERNEL_COMMON_HORIZONOS_ACPIPM_HPP

#include "Types.hpp"

#include "uacpi/acpi.h"
#include "uacpi/io.h"

#include "hal/Clock.hpp"

namespace kernel::common::hal {
    constexpr u64 frequency = 3579545;

    class AcpiPM {
    public:
        AcpiPM() = default;
        ~AcpiPM() = default;

        void init();

        u64 read() const;

        bool supported();

        bool isInit();

        static void calibrate(u64 ms);

        static u64 getNs();

    private:
        u64 readInternal() const;

        static u64 mask;
        static i64 offset;

        bool initialized {};

        acpi_gas timerBlock {};
        uacpi_mapped_gas *timerBlockMapped {};

        Clock clock {};
    };
}

#endif
