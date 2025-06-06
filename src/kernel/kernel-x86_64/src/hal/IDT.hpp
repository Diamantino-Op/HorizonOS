#ifndef KERNEL_X86_64_IDT_HPP
#define KERNEL_X86_64_IDT_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
	enum GateType : u8 {
		INTERRUPT_GATE = 0xE,
		TRAP_GATE = 0xF,
	};

	constexpr u8 IDT_ENTRY_PRESENT = 0b10000000;

    struct __attribute__((packed)) IDTEntry {
        u16 offsetLow {};
        u16 selector {};
        u8 ist : 3 {};
        u8 reservedLow : 5 {};
        u8 flags {};
        u16 offsetMid {};
        u32 offsetHigh {};
        u32 reservedHigh {};

        constexpr IDTEntry() = default;

        constexpr IDTEntry(const usize handler, const u16 selector, const u8 ist, const u8 flags):
    		offsetLow(handler & 0xffff),
    		selector(selector),
    		ist(ist),
    		flags(flags),
    		offsetMid((handler >> 16) & 0xffff),
    		offsetHigh(handler >> 32) {}
    };

	struct Idt {
		IDTEntry entries[256];

		constexpr Idt() = default;
	};

    struct __attribute__((packed)) IDTDesc {
		u16 limit{};
		u64 base{};

    	constexpr IDTDesc() = default;

		explicit constexpr IDTDesc(Idt const& base):
    		limit(sizeof(Idt) - 1),
    		base(reinterpret_cast<usize>(&base)) {}
	};

	class IdtManager {
	public:
		IdtManager();

		void addEntry(u8 id, usize handler, u16 selector, u8 ist, u8 flags);
		void loadIdt();

		Idt getIdt() const;

	private:
		Idt idtInstance{};
		IDTDesc idtDescriptor{};
	};

	extern "C" void loadIdtAsm(IDTDesc *idtDescriptor);
}

#endif