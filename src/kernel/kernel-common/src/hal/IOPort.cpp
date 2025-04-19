#include "IOPort.hpp"

namespace kernel::common::hal {
	void IOPort::out8(u8 data, u16 address) {
		asm volatile ("outb %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out16(u16 data, u16 address) {
		asm volatile ("outw %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out32(u32 data, u16 address) {
		asm volatile ("outl %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out64(u64 data, u16 address) {
		out32(data, address);
		out32(data >> 32, address + 4);
	}

	u8 IOPort::in8(u16 address) {
		u8 ret = 0;
		asm volatile ("inb %1, %0" : "=a"(ret) : "d"(address));

		return ret;
	}

	u16 IOPort::in16(u16 address) {
		u16 ret = 0;
		asm volatile ("inw %1, %0" : "=a"(ret) : "d"(address));

		return ret;
	}

	u32 IOPort::in32(u16 address) {
		u32 ret = 0;
		asm volatile ("inl %1, %0" : "=a"(ret) : "d"(address));

		return ret;
	}

	u64 IOPort::in64(u16 address) {
		u64 ret = in32(address);
		ret |= static_cast<u64>(in32(address + 4)) << 32;

		return ret;
	}

}