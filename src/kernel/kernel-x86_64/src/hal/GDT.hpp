#ifndef KERNEL_X86_64_GDT_HPP
#define KERNEL_X86_64_GDT_HPP

#include "Types.hpp"
#include "TSS.hpp"

namespace kernel::x86_64::hal {
	enum Selector : u8 {
		ZERO = 0x00,
		KERNEL_CODE = 0x08,
		KERNEL_DATA = 0x10,
		USER_CODE32 = 0x18,
		USER_DATA = 0x20,
		USER_CODE64 = 0x28,
		TSS = 0x30,
	};

	// User: Ring 3, Driver: Ring 2, System: Ring 1
	enum AccessBytes : u8 {
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

	enum Flags : u8 {
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

		explicit GdtEntry(const u8 accessByte, const u8 flags):
			accessByte(accessByte),
			flags(flags) {}
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

		/*explicit GdtTssEntry(Tss *tss):
			limitLow(sizeof(Tss)),
			baseLow(reinterpret_cast<usize>(tss) & 0xffff),
			baseMid((reinterpret_cast<usize>(tss) >> 16) & 0xff),
			accessByte(0b10001001),
			baseHigh((reinterpret_cast<usize>(tss) >> 24) & 0xff),
			baseUpper32(reinterpret_cast<usize>(tss) >> 32) {}*/

		explicit GdtTssEntry(Tss *tss) {
			const usize base  = reinterpret_cast<usize>(tss);
			const u32   limit = sizeof(Tss) - 1;

			limitLow    = limit & 0xFFFF;
			limitHigh   = (limit >> 16) & 0xF;
			baseLow     = base & 0xFFFF;
			baseMid     = (base >> 16) & 0xFF;
			baseHigh    = (base >> 24) & 0xFF;
			baseUpper32 = base >> 32;
			accessByte  = 0b10001001;
			flags       = 0;
		}

		explicit GdtTssEntry(TssTemp *tss):
			limitLow(sizeof(TssTemp)),
			baseLow(reinterpret_cast<usize>(tss) & 0xffff),
			baseMid((reinterpret_cast<usize>(tss) >> 16) & 0xff),
			accessByte(0b10001001),
			baseHigh((reinterpret_cast<usize>(tss) >> 24) & 0xff),
			baseUpper32(reinterpret_cast<usize>(tss) >> 32) {}
	};

	struct __attribute__((packed)) Gdt {
		GdtEntry entries[6];

		GdtTssEntry tssEntry{};

		constexpr Gdt() = default;
	};

	struct __attribute__((packed)) GdtDesc {
		u16 limit{};
		u64 base{};

		constexpr GdtDesc() = default;

		explicit GdtDesc(Gdt const& base):
			limit(sizeof(Gdt) - 1),
			base(reinterpret_cast<usize>(&base)) {}
	};

	class GdtManager {
	public:
		GdtManager() = default;
		explicit GdtManager(TssTemp *tss);

		void loadGdt();
		void reloadRegisters();

		Gdt &getGdt();

	private:
		Gdt gdtInstance{};
		GdtDesc gdtDescriptor{};
	};

	extern "C" void loadGdtAsm(GdtDesc* gdtDescriptor);
	extern "C" void reloadRegistersAsm();
}

#endif
