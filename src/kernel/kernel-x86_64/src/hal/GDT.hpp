#ifndef GDT_HPP
#define GDT_HPP

#include "Types.hpp"

namespace hal::x86_64 {
	struct __attribute__((packed)) Tss {
		u32 _reserved;
		Array<u64, 3> rsp;
		u64 _reserved1;
		Array<u64, 7> ist;
		u32 _reserved2;
		u32 _reserved3;
		u16 _reserved4;
		u16 iopbOffset;
	};

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
		u16 len;
		u16 baseLow16;
		u8 baseMid8;
		u8 flags1;
		u8 flags2;
		u8 baseHigh8;
		u32 baseUpper32;
		u32 _reserved;

		constexpr GdtTssEntry() = default;

		explicit GdtTssEntry(Tss const& tss)
			: len(sizeof(Tss)),
			  baseLow16((size_t)&tss & 0xffff),
			  baseMid8(((size_t)&tss >> 16) & 0xff),
			  flags1(0b10001001),
			  flags2(0),
			  baseHigh8(((size_t)&tss >> 24) & 0xff),
			  baseUpper32((size_t)&tss >> 32),
			  _reserved() {}
	};

	struct __attribute__((packed)) Gdt {
		static constexpr size_t LEN = 6;

		GdtEntry entries[6];

		/*Karm::Array<GdtEntry, Gdt::LEN - 1> entries = {
			GdtEntry{},
			{AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::EXECUTABLE, Flags::LONG_MODE},
			{AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE, 0},
			{AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::USER, 0},
			{AccessBytes::PRESENT | AccessBytes::CD_SEGMENT | AccessBytes::READ_WRITE | AccessBytes::EXECUTABLE | AccessBytes::USER, Flags::LONG_MODE},
		};*/

		GdtTssEntry tssEntry;

		explicit Gdt(Tss const& tss): tssEntry(tss) {}
	};

	struct __attribute__((packed)) GdtDesc {
		u16 limit;
		u64 base;

		explicit GdtDesc(Gdt const& base): limit(sizeof(Gdt) - 1), base(reinterpret_cast<size_t>(&base)) {}
	};

	class GdtManager {
		public:
			void initGdt();
			void initTss();
			void loadGdt();
			void updateTss();
			Gdt getGdt();
			Tss getTss();

		private:
			void addGdtEntry(u8 flags, u8 granularity);
			void addTssEntry();

			Gdt gdtInstance;
			GdtDesc gdtDescriptor;
			Tss tssInstance;
	};
}

#endif
