#include "Apic.hpp"

#include "CommonMain.hpp"
#include "Cpu.hpp"
#include "Math.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"
#include "utils/MMIO.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	bool Apic::isInitialized() const {
		return this->initialized;
	}

	void Apic::init() {
		Terminal *terminal = CommonMain::getTerminal();

		u64 apicBase = Asm::rdmsr(ApicMsrs::APIC_BASE);

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

		Asm::wrmsr(ApicMsrs::APIC_BASE, apicBase);

		if (not this->isX2Apic) {
			if (isBsp) {
				this->mmio = reinterpret_cast<u64>(CommonMain::getInstance()->getVPA()->allocVPages(1));

				terminal->debug("Mapping Hpet at address:", "Apic");
				terminal->debug("	Phys: 0x%.16lx", "Apic", this->physMmio);
				terminal->debug("	Virt: 0x%.16lx", "Apic", this->mmio);

				CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(this->mmio, this->physMmio, 0b00000011, true, false);
			}
		}

		// Enable external interrupts
		this->write(ApicMsrs::TPR, 0);

		// Enable APIC and set the spurious interrupt vector to 0xFF
		this->write(ApicMsrs::SIV, (1 << 8) | 0xFF);

		/*if (not isBsp) {
			this->calibrateTimer();
		}*/
		this->calibrateTimer();
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

			this->write(ApicMsrs::TDC, 0b1011);

			constexpr u64 times = 3;

			for (u64 i = 0; i < times; i++) {
				constexpr u64 millis = 50;

				this->write(ApicMsrs::TIC, 0xFFFFFFFF);

				calibrator(millis);

				const u32 count = this->read(ApicMsrs::TCC);

				this->write(ApicMsrs::TIC, 0);

				freq += (0xFFFFFFFF - count) * 100;
			}

			freq /= times;

			this->calibrated = true;
		}

		auto [val1, val2] = freq2NsPN(freq);

		this->p = val1;
		this->n = val2;
	}

	void Apic::eoi() {
		this->write(0xB0, 0);
	}

	void Apic::ipi(const u8 id, const Dest dsh, const u8 vector) {
		const auto flags = (static_cast<u32>(dsh) << 18) | vector;

		if (not this->isX2Apic) {
			write(ApicMsrs::ICRH, static_cast<u32>(id) << 24);
			write(ApicMsrs::ICRL, flags);
		} else {
			write(ApicMsrs::ICRL, (static_cast<u64>(id) << 32) | flags);
		}
	}

	void Apic::arm(u64 ns, u8 vector) {
		if (ns == 0) {
			ns = 1;
		}

		if (this->tscDeadline) {
			const u64 tscVal = CpuManager::getCurrentCore()->tsc.read();

			auto [p, n] = CpuManager::getCurrentCore()->tsc.getPN();

			u64 tscTicks = ns2Ticks(ns, p, n);

			this->write(ApicMsrs::LVT, (0b10 << 17) | vector);

			asm volatile ("mfence" ::: "memory");

			Asm::wrmsr(ApicMsrs::DEADLINE, tscVal + tscTicks);
		} else {
			this->write(ApicMsrs::TIC, 0);
			this->write(ApicMsrs::LVT, vector);

			u64 ticks = ns2Ticks(ns, this->p, this->n);

			this->write(ApicMsrs::TIC, ticks);
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
}