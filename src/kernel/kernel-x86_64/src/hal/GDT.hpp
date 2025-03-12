#ifndef KERNEL_X86_64_GDT_HPP
#define KERNEL_X86_64_GDT_HPP

#include "Types.hpp"
#include "TSS.hpp"

namespace hal::x86_64 {
	enum Selector {
		ZERO = 0,
		KERNEL_CODE = 1,
		KERNEL_DATA = 2,
		USER_DATA = 3,
		USER_CODE = 4,
		TSS = 5,
	};

	// User: Ring 3, Driver: Ring 2, System: Ring 1
	enum AccessBytes : u32 {
		PRESENT = 0b10000000,
		USER = 0b01100000,
		DRIVER = 0b01000000,
		SYSTEM = 0b00100000,
		CD_SEGMENT = 0b00010000,
		EXECUTABLE = 0b00001000,
		CONF_DIR = 0b00000100,
		READ_WRITE = 0b00000010,
		ACCESSED = 0b00000001,
	};

	enum Flags {
		PAGE_GRANULARITY = 0b1000,
		PROTECTED_SEGMENT = 0b0100,
		LONG_MODE = 0b0010,
	};

	// 64-Bit ignores limit and base values.
	struct __attribute__((packed)) GdtEntry {
		u16 limitLow{};
		u16 baseLow{};
		u8 baseMid{};
		u8 accessByte{};
		u8 limitHigh : 4 {};
		u8 flags : 4 {};
		u8 baseHigh{};

		constexpr GdtEntry() = default;

		explicit GdtEntry(u8 accessByte, u8 flags): accessByte(accessByte), flags(flags) {}
	};

	struct __attribute__((packed)) GdtTssEntry {
		u16 limitLow{};
		u16 baseLow{};
		u8 baseMid{};
		u8 accessByte{};
		u8 limitHigh : 4 {};
		u8 flags : 4 {};
		u8 baseHigh{};
		u32 baseUpper32{};
		u32 _reserved{};

		constexpr GdtTssEntry() = default;

		explicit GdtTssEntry(Tss const& tss): limitLow(sizeof(Tss)), baseLow((usize)&tss & 0xffff), baseMid(((usize)&tss >> 16) & 0xff), accessByte(0b10001001), baseHigh(((usize)&tss >> 24) & 0xff), baseUpper32((usize)&tss >> 32) {}
	};

	struct __attribute__((packed)) Gdt {
		GdtEntry entries[5];

		GdtTssEntry tssEntry{};

		constexpr Gdt() = default;
	};

	struct __attribute__((packed)) GdtDesc {
		u16 limit;
		u64 base;

		constexpr GdtDesc() = default;

		explicit GdtDesc(Gdt const& base): limit(sizeof(Gdt) - 1), base(reinterpret_cast<usize>(&base)) {}
	};

	class GdtManager {
		public:
			GdtManager() = default;
			~GdtManager() = default;

			void initGdt(Tss const& tss);
			void loadGdt();

			Gdt getGdt();

		private:
			Gdt gdtInstance{};
			GdtDesc gdtDescriptor{};
	};
}

#endif
