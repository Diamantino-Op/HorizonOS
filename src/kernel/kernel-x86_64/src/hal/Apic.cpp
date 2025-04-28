#include "Apic.hpp"

#include "utils/Asm.hpp"
#include "utils/MMIO.hpp"

#include "Cpu.hpp"

namespace kernel::x86_64::hal {
	using namespace utils;

	bool Apic::isInitialized() const {
		return this->initialized;
	}

	void Apic::init(bool bootstrap = false) {

	}

	void Apic::calibrateTimer() {

	}

	void Apic::eoi() {
		this->write(0xB0, 0);
	}

	void Apic::ipi(const u8 id, const Dest dsh, const u8 vector) {
		const auto flags = (static_cast<u32>(dsh) << 18) | vector;

		if (!this->isX2Apic) {
			write(ApicMsrs::ICRH, static_cast<u32>(id) << 24);
			write(ApicMsrs::ICRL, flags);
		} else {
			write(ApicMsrs::ICRL, (static_cast<u64>(id) << 32) | flags);
		}
	}

	void Apic::arm(usize ns, u8 vector) {
		if (ns == 0) {
			ns = 1;
		}

		if (this->tscDeadline) {

		} else {

		}
	}

	u32 Apic::toX2Apic(const u32 reg) {
		return (reg >> 4) + 0x800;
	}

	u32 Apic::read(const u32 reg) {
		if (this->isX2Apic) {
			return Asm::rdmsr(this->toX2Apic(reg));
		}

		return MMIO::in<u32>(mmio + reg);
	}

	void Apic::write(const u32 reg, const u32 data) {
		if (this->isX2Apic) {
			Asm::wrmsr(this->toX2Apic(reg), data);
		} else {
			MMIO::out<u32>(mmio + reg, data);
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

	void Apic::setCore(CpuCore *core) {
		this->core = core;
	}

	CpuCore *Apic::getCore() const {
		return this->core;
	}
}