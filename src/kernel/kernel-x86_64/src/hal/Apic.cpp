#include "Apic.hpp"

#include "utils/MMIO.hpp"
#include "CommonMain.hpp"
#include "Cpu.hpp"
#include "Main.hpp"
#include "Math.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace common::utils;
	using namespace utils;

	bool Apic::isInitialized() const {
		return this->initialized;
	}

	void Apic::init() {
		Terminal *terminal = CommonMain::getTerminal();

		u64 apicBase = Asm::rdmsr(ApicMsrs::LAPIC_BASE);

		const bool isBsp = apicBase & (1 << 8);

		this->physMmio = apicBase & ~0xFFF;

		if (isBsp) {
			terminal->debug("Supports X2Apic: %u", "Apic", this->isX2Apic);
		}

		// Global Enable
		apicBase |= (1 << 11);

		// Enable X2Apic
		if (this->isX2Apic) {
			apicBase |= (1 << 10);
		}

		Asm::wrmsr(ApicMsrs::LAPIC_BASE, apicBase);

		if (not this->isX2Apic) {
			if (isBsp) {
				this->mmio = reinterpret_cast<u64>(CommonMain::getInstance()->getVPA()->allocVPages(1));

				terminal->debug("Mapping Apic at address:", "Apic");
				terminal->debug("	Phys: 0x%.16lx", "Apic", this->physMmio);
				terminal->debug("	Virt: 0x%.16lx", "Apic", this->mmio);

				CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(this->mmio, this->physMmio, 0b00000011, true, false); // TODO: Maybe use MMIO flags
			}
		}

		// Enable external interrupts
		this->write(ApicMsrs::LAPIC_TPR, 0);

		// Enable APIC and set the spurious interrupt vector to 0xFF
		this->write(ApicMsrs::LAPIC_SIV, (1 << 8) | 0xFF);

		/*if (not isBsp) {
			this->calibrateTimer();
		}*/
		this->calibrateTimer();

		this->initialized = true;
	}

	void Apic::calibrateTimer() {
		if (this->tscDeadline) {
			CommonMain::getTerminal()->debug("Using TSC deadline!", "Apic");

			this->calibrated = true;

			return;
		}

		u64 freq = 0;

		if (const u32 ecx = CpuId::get(0x15, 0).ecx; ecx != 0) {
			freq = ecx;

			this->calibrated = true;
		} else {
			const CalibratorFun calibrator = CommonMain::getInstance()->getClocks()->getCalibrator();

			if (calibrator == nullptr) {
				CommonMain::getTerminal()->error("Could not calibrate timer!", "Apic");

				return;
			}

			this->write(ApicMsrs::LAPIC_TDC, 0b1011);

			constexpr u64 times = 3;

			for (u64 i = 0; i < times; i++) {
				constexpr u64 millis = 50;

				this->write(ApicMsrs::LAPIC_TIC, 0xFFFFFFFF);

				calibrator(millis);

				const u32 count = this->read(ApicMsrs::LAPIC_TCC);

				this->write(ApicMsrs::LAPIC_TIC, 0);

				freq += (0xFFFFFFFF - count) * 100;
			}

			freq /= times;

			this->calibrated = true;
		}

		CommonMain::getTerminal()->debug("Timer frequency: %lu Hz", "Apic", freq);

		auto [val1, val2] = freq2NsPN(freq);

		this->p = val1;
		this->n = val2;
	}

	void Apic::eoi() {
		this->write(ApicMsrs::LAPIC_EOI, 0);
	}

	void Apic::ipi(const u8 id, const Dest dsh, const u8 vector) {
		const auto flags = (static_cast<u32>(dsh) << 18) | vector;

		if (not this->isX2Apic) {
			write(ApicMsrs::LAPIC_ICRH, static_cast<u32>(id) << 24);
			write(ApicMsrs::LAPIC_ICRL, flags);
		} else {
			write(ApicMsrs::LAPIC_ICRL, (static_cast<u64>(id) << 32) | flags);
		}
	}

	void Apic::arm(u64 ns, u8 vector, bool periodic) {
		if (ns == 0) {
			ns = 1;
		}

		if (this->p == 0) {
			CommonMain::getTerminal()->error("P val is 0! Setting to 1...", "Apic");

			this->p = 1;
		}

		if (this->n == 0) {
			CommonMain::getTerminal()->error("N val is 0! Setting to 1...", "Apic");

			this->n = 1;
		}

		if (this->tscDeadline and not periodic) {
			const u64 tscVal = CpuManager::getCurrentCore()->tsc.read();

			auto [p, n] = CpuManager::getCurrentCore()->tsc.getPN();

			u64 tscTicks = ns2Ticks(ns, p, n);

			this->write(ApicMsrs::LAPIC_LVT, (0b10 << 17) | vector);

			asm volatile ("mfence" ::: "memory");

			Asm::wrmsr(ApicMsrs::LAPIC_DEADLINE, tscVal + tscTicks);
		} else {
			this->write(ApicMsrs::LAPIC_TIC, 0);

			if (periodic) {
				this->write(ApicMsrs::LAPIC_LVT, (0b1 << 17) | vector);
			} else {
				this->write(ApicMsrs::LAPIC_LVT, vector);
			}

			u64 ticks = ns2Ticks(ns, this->p, this->n);

			this->write(ApicMsrs::LAPIC_TIC, ticks);
		}
	}

	u32 Apic::toX2Apic(const u32 reg) {
		return (reg >> 4) + 0x800;
	}

	u32 Apic::read(const u32 reg) {
		if (this->isX2Apic) {
			return Asm::rdmsr(this->toX2Apic(reg));
		}

		return MMIO::in<u32>(this->mmio + reg);
	}

	void Apic::write(const u32 reg, const u32 data) {
		if (this->isX2Apic) {
			Asm::wrmsr(this->toX2Apic(reg), data);
		} else {
			MMIO::out<u32>(this->mmio + reg, data);
		}
	}

	void Apic::setId(const u32 apicId) {
		this->apicId = apicId;
	}

	u32 Apic::getId() const {
		return this->apicId;
	}

	void Apic::setIsX2Apic(const bool val) {
		this->isX2Apic = val;
	}

	bool Apic::getIsX2Apic() const {
		return this->isX2Apic;
	}

	// IOApic

	void IOApic::init(u64 physMmio, u32 gsiBase) {
		Terminal *terminal = CommonMain::getTerminal();

		this->mmio = reinterpret_cast<u64>(CommonMain::getInstance()->getVPA()->allocVPages(1));

		terminal->debug("Mapping IOApic at address:", "IOApic");
		terminal->debug("	Phys: 0x%.16lx", "IOApic", physMmio);
		terminal->debug("	Virt: 0x%.16lx", "IOApic", this->mmio);

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(this->mmio, physMmio, 0b00000011, true, false); // TODO: Maybe use MMIO flags

		this->redirects = ((this->read(0x01) >> 16) & 0xFF) + 1;

		for (u64 i = 0; i < this->redirects; i++) {
			this->maskIdx(i);
		}
	}

	void IOApic::setIdx(const u64 idx, const u8 vector, const u64 dest, const u16 flags, const IOApicDelivery delivery) {
		u64 entry = 0;

		entry |= vector;
		entry |= delivery & IOApicDelivery::EXT_INT;
		entry |= flags & ~0x7FF;
		entry |= dest << 56;

		this->writeEntry(idx, entry);
	}

	void IOApic::maskIdx(const u64 idx) {
		u64 entry = readEntry(idx);

		entry |= 1 << 16;

		writeEntry(idx, entry);
	}

	void IOApic::unmaskIdx(const u64 idx) {
		u64 entry = readEntry(idx);

		entry |= ~(1 << 16);

		writeEntry(idx, entry);
	}

	u32 IOApic::entry(const u32 idx) {
		return 0x10 + (idx * 2);
	}

	u32 IOApic::read(const u32 reg) const {
		MMIO::out<u32>(this->mmio, reg);

		return MMIO::in<u32>(this->mmio + 0x10);
	}

	void IOApic::write(const u32 reg, const u32 data) const {
		MMIO::out<u32>(this->mmio, reg);
		MMIO::out<u32>(this->mmio + 0x10, data);
	}

	u64 IOApic::readEntry(const u32 idx) {
		const u32 lo = this->read(this->entry(idx));
		const u32 hi = this->read(this->entry(idx + 1));

		return (static_cast<u64>(hi) << 32) | lo;
	}

	void IOApic::writeEntry(const u32 idx, const u64 data) {
		this->write(this->entry(idx), data & 0xFFFFFFFF);
		this->write(this->entry(idx + 1), data >> 32);
	}

	Pair IOApic::getGsiRange() const {
		return { this->gsiBase, this->gsiBase + this->redirects };
	}

	// IOApic Manager

	void IOApicManager::init() {
		reinterpret_cast<Kernel *>(CommonMain::getInstance())->getDualPic()->disable();

		this->ioApics = new IOApic[CommonMain::getInstance()->getUAcpi()->getIoApicsAmount()];

		for (u64 i = 0; i < CommonMain::getInstance()->getUAcpi()->getIoApicsAmount(); i++) {
			const acpi_madt_ioapic entry = CommonMain::getInstance()->getUAcpi()->getIoApics()[i];

			this->ioApics[i].init(entry.address, entry.gsi_base);

			auto [start, end] = this->ioApics[i].getGsiRange();

			if (end > this->maxRange) {
				this->maxRange = end;
			}

			CommonMain::getTerminal()->debug("IOApic %lu gsi range: %lu - %lu", "IOApic", i, start, end);
		}

		if (CommonMain::getInstance()->getUAcpi()->getMadtTable()->flags & ACPI_PIC_ENABLED) {
			for (u8 j = 0; j < this->maxRange; j++) {
				if (j == 2) {
					continue;
				}

				for (u64 k = 0; k < CommonMain::getInstance()->getUAcpi()->getIsosAmount(); k++) {
					if (const acpi_madt_interrupt_source_override entry = CommonMain::getInstance()->getUAcpi()->getIsos()[k]; entry.source == j) {
						this->setGsi(entry.gsi, entry.source + 0x20, reinterpret_cast<Kernel *>(CommonMain::getInstance())->getCpuManager()->getBootstrapCpu()->apic.getId(), entry.flags | IOApicFlags::MASKED, IOApicDelivery::FIXED);

						goto end;
					}
				}

				this->setGsi(j, j + 0x20, reinterpret_cast<Kernel *>(CommonMain::getInstance())->getCpuManager()->getBootstrapCpu()->apic.getId(), static_cast<u16>(IOApicFlags::MASKED), IOApicDelivery::FIXED);

				end:
			}
		}

		this->initialized = true;
	}

	void IOApicManager::setGsi(const u64 gsi, const u8 vector, const u64 dest, const u16 flags, const IOApicDelivery delivery) const {
		CommonMain::getTerminal()->debug("Redirecting gsi %lu to vector %u", "IOApic", gsi, vector);

		IOApic *entry = this->gsiToIOApic(gsi);

		entry->setIdx(gsi - entry->getGsiRange().val1, vector, dest, flags, delivery);
	}

	void IOApicManager::maskGsi(const u32 gsi) const {
		IOApic *entry = this->gsiToIOApic(gsi);

		entry->maskIdx(gsi - entry->getGsiRange().val1);
	}

	void IOApicManager::unmaskGsi(const u32 gsi) const {
		IOApic *entry = this->gsiToIOApic(gsi);

		entry->unmaskIdx(gsi - entry->getGsiRange().val1);
	}

	// TODO: Gsi might normally be 0
	void IOApicManager::mask(const u8 vector) {
		if (vector < 0x20) {
			return;
		}

		if (const u32 gsi = this->irqToIso(vector - 0x20); gsi < 1'000'000) {
			this->maskGsi(gsi);
		} else {
			this->maskGsi(vector - 0x20);
		}
	}

	void IOApicManager::unmask(const u8 vector) {
		if (vector < 0x20) {
			return;
		}

		if (const u32 gsi = this->irqToIso(vector - 0x20); gsi < 1'000'000) {
			this->unmaskGsi(gsi);
		} else {
			this->unmaskGsi(vector - 0x20);
		}
	}

	IOApic *IOApicManager::gsiToIOApic(const u32 gsi) const {
		for (u64 i = 0; i < CommonMain::getInstance()->getUAcpi()->getIoApicsAmount(); i++) {
			IOApic *entry = &this->ioApics[i];

			if (auto [start, end] = entry->getGsiRange(); start < gsi and gsi <= end) {
				return entry;
			}
		}

		__builtin_unreachable();
	}

	u32 IOApicManager::irqToIso(const u8 irq) {
		for (u64 k = 0; k < CommonMain::getInstance()->getUAcpi()->getIsosAmount(); k++) {
			if (const acpi_madt_interrupt_source_override entry = CommonMain::getInstance()->getUAcpi()->getIsos()[k]; entry.source == irq) {
				return entry.gsi;
			}
		}

		return 1'000'000;
	}

	bool IOApicManager::isInitialized() const {
		return this->initialized;
	}

	u8 IOApicManager::getMaxRange() const {
		return this->maxRange;
	}
}