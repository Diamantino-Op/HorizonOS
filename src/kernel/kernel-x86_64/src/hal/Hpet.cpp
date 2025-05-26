#include "Hpet.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "Cpu.hpp"
#include "Math.hpp"

#include "uacpi/tables.h"

namespace kernel::x86_64::hal {
	using namespace common;

	u64 Hpet::p;
	u64 Hpet::n;
	u64 Hpet::offset;

	void Hpet::init() {
		Terminal *terminal = CommonMain::getTerminal();

		if (not supported()) {
			terminal->debug("Hpet not supported!", "Hpet");

			return;
		}

		u64 *virtAddr = CommonMain::getInstance()->getKernelAllocContext()->pageMap.allocVPages(1);

		terminal->debug("Mapping Hpet at address: 0x%.16lx", "Hpet", virtAddr);

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(reinterpret_cast<u64>(virtAddr), physAddr, 0b00000011, true, false);

		const u64 cap = read(regCap);

		this->is64Bit = (cap & ACPI_HPET_COUNT_SIZE_CAP);

		this->frequency = 1'000'000'000'000'000ull / (cap >> 32);

		auto [val1, val2] = freq2NsPN(this->frequency);

		p = val1;
		n = val2;

		this->write(regCfg, 1);

		this->initialized = true;

		if (const Clock *clock = CommonMain::getInstance()->getClocks()->getMainClock()) {
			offset = getNs() - clock->getNs();
		}

		this->clock = {
			.name = "Hpet",
			.priority = 25,
			.getNs = &Hpet::getNs,
		};

		CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
	}

	bool Hpet::supported() {
		uacpi_table table;

		if (uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &table) != UACPI_STATUS_OK) {
			return false;
		}

		const acpi_hpet *hpet = static_cast<acpi_hpet *>(table.ptr);

		if (hpet->address.address_space_id != UACPI_ADDRESS_SPACE_SYSTEM_MEMORY) {
			uacpi_table_unref(&table);

			return false;
		}

		this->physAddr = hpet->address.address;

		uacpi_table_unref(&table);

		return true;
	}

	u64 Hpet::read(u64 offset) {

	}

	u64 Hpet::read() {

	}

	void Hpet::write(u64 offset, u64 val) {

	}

	bool isInitialized() {

	}

	void Hpet::calibrate(u64 ms) {

	}

	u64 Hpet::getNs() {
		return ticks2ns(reinterpret_cast<Kernel *>(CommonMain::getInstance())->getHpet()->read(), p, n) - offset;
	}
}