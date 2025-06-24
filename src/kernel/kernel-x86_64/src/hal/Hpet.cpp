#include "Hpet.hpp"

#include "utils/MMIO.hpp"
#include "CommonMain.hpp"
#include "Main.hpp"
#include "Math.hpp"
#include "stdatomic.h"

#include "uacpi/tables.h"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace common::utils;
	using namespace utils;

	u64 Hpet::p;
	u64 Hpet::n;
	u64 Hpet::offset;
	u64 Hpet::lastReadVal;
	u64 Hpet::frequency;

	void Hpet::init() {
		Terminal *terminal = CommonMain::getTerminal();

		if (not supported()) {
			terminal->debug("Hpet not supported!", "Hpet");

			return;
		}

		this->virtAddr = reinterpret_cast<u64>(CommonMain::getInstance()->getVPA()->allocVPages(1));

		terminal->debug("Mapping Hpet at address:", "Hpet");
		terminal->debug("	Phys: 0x%.16lx", "Hpet", this->physAddr);
		terminal->debug("	Virt: 0x%.16lx", "Hpet", this->virtAddr);

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(this->virtAddr, this->physAddr, 0b00000011, true, false); // TODO: Maybe use MMIO flags

		const u64 cap = read(regCap);

		terminal->debug("Cap low val: 0x%.16lx", "Hpet", cap);

		this->is64Bit = (cap & ACPI_HPET_COUNT_SIZE_CAP);

		this->maxTimers = ((cap >> ACPI_HPET_NUMBER_OF_COMPARATORS_SHIFT) & ACPI_HPET_NUMBER_OF_COMPARATORS_MASK);

		for (u8 i = 0; i < this->maxTimers; i++) {
			terminal->debug("Timer %u supports periodic: %u", "Hpet", i, this->read(getTimerRegister(i)) >> 5 & 0b1);
		}

		terminal->debug("Max timers: %u", "Hpet", this->maxTimers);

		frequency = 1'000'000'000'000'000ull / (cap >> 32);

		CommonMain::getTerminal()->debug("Timer frequency: %lu Hz", "Hpet", frequency);

		auto [val1, val2] = freq2NsPN(frequency);

		p = val1;
		n = val2;

		this->write(regCfg, 1);

		this->initialized = true;

		if (const Clock *mainClock = CommonMain::getInstance()->getClocks()->getMainClock()) {
			offset = getNs() - mainClock->getNs();
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

	u64 Hpet::read(const u64 offset) const {
		return MMIO::in<u64>(this->virtAddr + offset);
	}

	u64 Hpet::read() const {
		constexpr u64 mask = 0xFFFFFFFFul;

		u64 value = read(regCnt);

		if (is64Bit) {
			return value;
		}

		u64 lastVal = __atomic_load_n(&lastReadVal, __ATOMIC_RELAXED);

		value &= mask;
		value |= lastVal & ~mask;

		if (value < lastVal) {
			value += (1ul << 32);
		}

		if (lastVal - value > (mask >> 1)) {
			__atomic_compare_exchange_n(&lastVal, &lastVal, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
		}

		return value;
	}

	void Hpet::write(const u64 offset, const u64 val) const {
		MMIO::out<u64>(this->virtAddr + offset, val);
	}

	bool Hpet::isInitialized() const {
		return this->initialized;
	}

	u8 Hpet::getMaxTimers() const {
		return this->maxTimers;
	}

	u64 Hpet::getFrequency() const {
		return frequency;
	}

	u64 Hpet::getTimerRegister(const u8 timerId) {
		return regTmrStart + (0x20 * timerId);
	}

	u64 Hpet::getComparatorRegister(const u8 timerId) {
		return regCmpStart + (0x20 * timerId);
	}

	void Hpet::calibrate(const u64 ms) {
		const Hpet *hpetPtr = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getHpet();

		const u64 ticks = (ms * frequency) / 1'000;

		const u64 start = hpetPtr->read();
		u64 current = start;

		while (current < start + ticks) {
			current = hpetPtr->read();
		}
	}

	u64 Hpet::getNs() {
		return ticks2ns(reinterpret_cast<Kernel *>(CommonMain::getInstance())->getHpet()->read(), p, n) - offset;
	}
}