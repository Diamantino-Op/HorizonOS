#include "PIC.hpp"

namespace kernel::x86_64::hal {
	PIC::PIC(const u8 address) : address(address) {}

	void PIC::ack() const {
		this->cmd(0x20, false);
	}

	void PIC::disable() const {
		this->data(0xff, false);
	}

	void PIC::cmd(u8 cmd, bool needsWait) const {
		asm volatile ("outb %0, %1" : : "a"(cmd), "Nd"(static_cast<u16>(address + commandAddress)));

		if (needsWait) {
			this->wait();
		}
	}

	void PIC::data(u8 data, bool needsWait) const {
		asm volatile ("outb %0, %1" : : "a"(data), "Nd"(static_cast<u16>(address + commandAddress)));

		if (needsWait) {
			this->wait();
		}
	}

	void PIC::wait() const {
		asm volatile("jmp 1f; 1: jmp 1f; 1:");
	}

	DualPIC::DualPIC() : pic1(pic1Address), pic2(pic2Address) {}

	void DualPIC::init() const {
		this->pic1.cmd(icw1Init | icw1Icw4, true);
		this->pic2.cmd(icw1Init | icw1Icw4, true);

		this->pic1.data(pic1Offset, true);
		this->pic2.data(pic2Offset, true);

		this->pic1.data(4, true);
		this->pic2.data(2, true);

		this->pic1.data(0x01, true);
		this->pic2.data(0x01, true);

		this->pic1.data(0x00, true);
		this->pic2.data(0x00, true);
	}

	void DualPIC::ack(const usize intNo) const {
		if (intNo >= 40) {
			this->pic2.ack();
		}

		this->pic1.ack();
	}

	void DualPIC::disable() const {
		this->pic1.disable();
		this->pic2.disable();
	}
}