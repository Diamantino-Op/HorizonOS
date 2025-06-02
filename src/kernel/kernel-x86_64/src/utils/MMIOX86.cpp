#include "utils/MMIO.hpp"

namespace kernel::common::utils {
	u64 MMIO::read(u64 addr, usize width) {
		switch (width) {
			case sizeof(u8): {
				u8 value;

				asm volatile ("mov %1, %0" : "=q"(value) : "m"(*reinterpret_cast<volatile u8 *>(addr)) : "memory");

				return value;
			}

			case sizeof(u16): {
				u16 value;

				asm volatile ("mov %1, %0" : "=q"(value) : "m"(*reinterpret_cast<volatile u16 *>(addr)) : "memory");

				return value;
			}

			case sizeof(u32): {
				u32 value;

				asm volatile ("mov %1, %0" : "=q"(value) : "m"(*reinterpret_cast<volatile u32 *>(addr)) : "memory");

				return value;
			}

			case sizeof(u64): {
				u64 value;

				asm volatile ("mov %1, %0" : "=r"(value) : "m"(*reinterpret_cast<volatile u64 *>(addr)) : "memory");

				return value;
			}

			default:
				return 0; // TODO: Replace with panic
		}
	}

	void MMIO::write(u64 addr, u64 data, usize width) {
		switch (width) {
			case sizeof(u8):
				asm volatile ("mov %0, %1" :: "q"(data), "m"(*reinterpret_cast<volatile u8 *>(addr)) : "memory");
				break;

			case sizeof(u16):
				asm volatile ("mov %0, %1" :: "r"(data), "m"(*reinterpret_cast<volatile u16 *>(addr)) : "memory");
				break;

			case sizeof(u32):
				asm volatile ("mov %0, %1" :: "r"(data), "m"(*reinterpret_cast<volatile u32 *>(addr)) : "memory");
				break;

			case sizeof(u64):
				asm volatile ("mov %0, %1" :: "r"(data), "m"(*reinterpret_cast<volatile u64 *>(addr)) : "memory");
				break;

			default:
				return; // TODO: Replace with panic
		}
	}
}