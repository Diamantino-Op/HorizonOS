#include "IOPort.hpp"

namespace kernel::x86_64::hal {
	void IOPort::out8(u8 data, u16 address) {
		asm volatile ("outb %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out16(u16 data, u16 address) {
		asm volatile ("outw %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out32(u32 data, u16 address) {
		asm volatile ("outl %0, %1" : : "a"(data), "d"(address));
	}

	void IOPort::out64(const u64 data, const u16 address) {
		out32(data, address);
		out32(data >> 32, address + 4);
	}

	void IOPort::outd8(const u8 data, const u16 address) {
		ioWait();
		out8(data, address);
		ioWait();
	}

	void IOPort::outd16(const u16 data, const u16 address) {
		ioWait();
		out16(data, address);
		ioWait();
	}

	void IOPort::outd32(const u32 data, const u16 address) {
		ioWait();
		out32(data, address);
		ioWait();
	}

	void IOPort::outd64(const u64 data, const u16 address) {
		ioWait();
		out64(data, address);
		ioWait();
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

	u64 IOPort::in64(const u16 address) {
		u64 ret = in32(address);
		ret |= static_cast<u64>(in32(address + 4)) << 32;

		return ret;
	}

	u8 IOPort::ind8(const u16 address) {
		ioWait();
		const u8 ret = in8(address);
		ioWait();

		return ret;
	}

	u16 IOPort::ind16(const u16 address) {
		ioWait();
		const u16 ret = in16(address);
		ioWait();

		return ret;
	}

	u32 IOPort::ind32(const u16 address) {
		ioWait();
		const u32 ret = in32(address);
		ioWait();

		return ret;
	}

	u64 IOPort::ind64(const u16 address) {
		u64 ret = in32(address);
		ret |= static_cast<u64>(in32(address + 4)) << 32;

		return ret;
	}

	void IOPort::ioWait() {
		out8(0, 0x80);
	}
}
