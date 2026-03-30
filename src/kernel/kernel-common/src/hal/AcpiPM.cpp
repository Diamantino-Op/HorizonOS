#include "AcpiPM.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"

// TODO: Broken

namespace kernel::common::hal {
	u64 AcpiPM::mask;
	i64 AcpiPM::offset;
	u64 AcpiPM::lastExtendedTicks;

	void AcpiPM::init() {
		if (not supported()) {
			CommonMain::getTerminal()->debug("Acpi PM not supported!", "AcpiPM");

			return;
		}

		this->initialized = true;

		if (const Clock *currClock = CommonMain::getInstance()->getClocks()->getMainClock(); currClock != nullptr) {
			offset = getNs() - currClock->getNs();
		}

		this->clock = {
			.name = "AcpiPM",
			.priority = 50,
			.getNs = &AcpiPM::getNs,
		};

		CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
	}

	u64 AcpiPM::read() const {
		u32 v1 = 0;
		u32 v2 = 0;
		u32 v3 = 0;

		do {
			v1 = readInternal();
			v2 = readInternal();
			v3 = readInternal();
		} while (__builtin_expect(((v1 > v2 && v1 < v3) || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)), 0));

		return v2 & mask;
	}

	u64 AcpiPM::readInternal() const {
		u64 value;

		if (timerBlockMapped == nullptr) {
			return 0;
		}

		if (uacpi_gas_read_mapped(timerBlockMapped, &value) != UACPI_STATUS_OK) {
			return 0;
		}

		return value;
	}

	bool AcpiPM::supported() {
		const acpi_fadt *fadtTable = CommonMain::getInstance()->getUAcpi()->getFadtTable();

		if (fadtTable == nullptr) {
			return false;
		}

		if (fadtTable->pm_tmr_len != 4) {
			return false;
		}

		this->timerBlock = fadtTable->x_pm_tmr_blk;

		if (this->timerBlock.address == 0) {
			return false;
		}

		if (timerBlockMapped == nullptr) {
			if (uacpi_map_gas(&timerBlock, &timerBlockMapped) != UACPI_STATUS_OK) {
				return false;
			}
		}

		mask = (fadtTable->flags & (1 << 8)) ? 0xFFFFFFFF : 0xFFFFFF;

		return true;
	}

	bool AcpiPM::isInit() {
		return this->initialized;
	}

	void AcpiPM::calibrate(const u64 ms) {
		AcpiPM *currAcpiPM = CommonMain::getInstance()->getAcpiPM();
		const u64 wrapTicks = mask + 1;

		if (!currAcpiPM->supported() or (ms * frequency) / 1000 >= wrapTicks) {
			return;
		}

		const u64 ticks = (ms * frequency) / 1000;

		const u64 start = currAcpiPM->read();

		u64 current = start;

		while (true) {
			current = currAcpiPM->read();

			u64 elapsed = 0;

			if (current >= start) {
				elapsed = current - start;
			} else {
				elapsed = (wrapTicks - start) + current;
			}

			if (elapsed >= ticks) {
				break;
			}
		}
	}

	u64 AcpiPM::getNs() {
		AcpiPM *acpiPm = CommonMain::getInstance()->getAcpiPM();
		const u64 raw = acpiPm->read();
		u64 prev = __atomic_load_n(&lastExtendedTicks, __ATOMIC_RELAXED);
		u64 extended = 0;
		const u64 wrapTicks = mask + 1;

		while (true) {
			const u64 prevRaw = prev & mask;
			u64 base = prev & ~mask;

			if (raw < prevRaw && (prevRaw - raw) > (mask >> 1)) {
				base += wrapTicks;
			}

			extended = base + raw;

			if (extended < prev) {
				extended = prev;
			}

			if (__atomic_compare_exchange_n(&lastExtendedTicks, &prev, extended, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
				break;
			}
		}

		const auto [val1, val2] = freq2NsPN(frequency);

		return ticks2ns(extended, val1, val2) - offset;
	}
}