#ifndef GDT_HPP
#define GDT_HPP

#include "Types.hpp"

namespace hal::x86_64 {
	struct __attribute__((packed)) GdtEntry {
		u16 limitLow{};
		u16 baseLow{};
		u8 baseMid{};
		u8 flags{};
		u8 limitHigh : 4 {};
		u8 granularity : 4 {};
		u8 baseHigh{};

		constexpr GdtEntry() = default;

		constexpr GdtEntry(u8 flags, u8 granularity)
			: flags(flags),
			  granularity(granularity) {};

		constexpr GdtEntry(u32 base, u32 limit, u8 flags, u8 granularity)
			: limitLow(limit & 0xffff),
			  baseLow(base & 0xffff),
			  baseMid((base >> 16) & 0xff),
			  flags(flags),
			  limitHigh((limit >> 16) & 0x0f),
			  granularity(granularity),
			  baseHigh((base >> 24) & 0xff) {}
	};

    struct __attribute__((packed)) GDTDescriptor {
		u16 size;
    	u64 offset;
	};
}

#endif
