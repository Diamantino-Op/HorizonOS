#include "AcpiPM.hpp"

#include "CommonMain.hpp"

namespace kernel::x86_64::hal {
	using namespace common;

	void AcpiPM::init() {

	}

	u64 AcpiPM::read() {

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

	uacpi_interrupt_ret AcpiPM::handleOverflow(uacpi_handle) {

	}

	void AcpiPM::calibrate(u64 ms) {

	}

	u64 AcpiPM::getNs() {

	}
}