#include "PIC.hpp"

#include "IOPort.hpp"
#include "utils/Asm.hpp"

namespace kernel::x86_64::hal {
	using namespace utils;

	PIC::PIC(const u8 address) : address(address) {}

	void PIC::eoi() const {
		this->cmd(0x20);
	}

	void PIC::disable() const {
		this->dataOut(0xff);
	}

	void PIC::cmd(const u8 cmd) const {
		IOPort::out8(cmd, address + commandAddress);
	}

	void PIC::dataOut(const u8 data) const {
		IOPort::out8(data, address + dataAddress);
	}

	u8 PIC::dataIn() const {
		return IOPort::in8(address + dataAddress);
	}

	DualPIC::DualPIC() : pic1(pic1Address), pic2(pic2Address) {}

	void DualPIC::init() const {
		Asm::cli();

		this->pic1.cmd(icw1Init | icw1Icw4);
		this->pic2.cmd(icw1Init | icw1Icw4);

		this->pic1.dataOut(pic1Offset);
		this->pic2.dataOut(pic2Offset);

		this->pic1.dataOut(4);
		this->pic2.dataOut(2);

		this->pic1.dataOut(0x01);
		this->pic2.dataOut(0x01);

		this->pic1.dataOut(0x00);
		this->pic2.dataOut(0x00);

		Asm::sti();
	}

	void DualPIC::eoi(const usize intNo) const {
		if (intNo >= 40) {
			this->pic2.eoi();
		}

		this->pic1.eoi();
	}

	void DualPIC::disable() const {
		this->pic1.disable();
		this->pic2.disable();
	}

	void DualPIC::mask(const u8 id) const {
		if (u8 irq = id - 0x20; irq >= 8) {
			irq -= 8;

			pic2.dataOut(pic2.dataIn() | (1 << irq));
		} else {
			pic1.dataOut(pic1.dataIn() | (1 << irq));
		}
	}

	void DualPIC::unmask(const u8 id) const {
		if (u8 irq = id - 0x20; irq >= 8) {
			irq -= 8;

			pic2.dataOut(pic2.dataIn() & ~(1 << irq));
		} else {
			pic1.dataOut(pic1.dataIn() & ~(1 << irq));
		}
	}
}