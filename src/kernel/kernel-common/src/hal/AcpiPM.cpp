#include "AcpiPM.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"

namespace kernel::common::hal {
	u64 AcpiPM::mask;
	i64 AcpiPM::offset;

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
		u64 value;

		uacpi_gas_read_mapped(timerBlockMapped, &value);

		return value;
	}

	bool AcpiPM::supported() {
		acpi_fadt *fadtTable = CommonMain::getInstance()->getUAcpi()->getFadtTable();

		if (fadtTable == nullptr) {
			return false;
		}

		if (fadtTable->pm_tmr_len != 4) {
			return false;
		}

		this->timerBlock = fadtTable->x_pm_tmr_blk;

		uacpi_map_gas(&timerBlock, &timerBlockMapped);

		mask = (fadtTable->flags & (1 << 8)) ? 0xFFFFFFFF : 0xFFFFFF;

		return true;
	}

	void AcpiPM::calibrate(const u64 ms) {
		AcpiPM *currAcpiPM = CommonMain::getInstance()->getAcpiPM();

		if (!currAcpiPM->supported() or (ms * frequency) / 1000 > mask) {
			return;
		}

		const u64 ticks = (ms * frequency) / 1000;

		const u64 start = currAcpiPM->read();

		u64 current = start;

		while (current < start + ticks) {
			current = currAcpiPM->read();

			if (current < start) {
				current += mask;
			}
		}
	}

	u64 AcpiPM::getNs() {
		const auto [val1, val2] = freq2NsPN(frequency);

		return ticks2ns(CommonMain::getInstance()->getAcpiPM()->read(), val1, val2) - offset;
	}
}