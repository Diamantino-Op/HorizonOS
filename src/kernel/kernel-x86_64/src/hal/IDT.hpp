#ifndef KERNEL_X86_64_IDT_HPP
#define KERNEL_X86_64_IDT_HPP

#include "Types.hpp"

namespace x86_64::hal {
	enum GateType : u8 {
		TRAP_TYPE = 0xEF,
		USER_TYPE = 0x60,
		GATE_TYPE = 0x8E,
	};

	struct __attribute__((packed)) Frame {
		u64 r15;
		u64 r14;
		u64 r13;
		u64 r12;
		u64 r11;
		u64 r10;
		u64 r9;
		u64 r8;
		u64 rbp;
		u64 rdi;
		u64 rsi;
		u64 rdx;
		u64 rcx;
		u64 rbx;
		u64 rax;

		u64 intNo;
		u64 errNo;

		u64 rip;
		u64 cs;
		u64 rFlags;
		u64 rsp;
		u64 ss;
	};

    struct __attribute__((packed)) IDTEntry {
        u16 offsetLow{};
        u16 selector{};
        u8 ist : 3 {};
        u8 reservedLow : 5 {};
        u8 flags{};
        u16 offsetMid{};
        u32 offsetHigh{};
        u32 reservedHigh{};

        constexpr IDTEntry() = default;

        constexpr IDTEntry(usize handler, u16 selector, u8 ist, u8 flags):
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

    	constexpr IDTDesc(Idt const& base):
    		limit(sizeof(Idt) - 1),
    		base(reinterpret_cast<usize>(&base)) {}
	};

	class IDTManager {
	public:
		IDTManager();

		void addEntry(u8 id, usize handler, u16 selector, u8 ist, u8 flags);
		void loadIdt();

		Idt getIdt();

	private:
		Idt idtInstance{};
		IDTDesc idtDescriptor{};
	};

	extern "C" void loadIdtAsm(IDTDesc *idtDescriptor);
	extern "C" void handleInterruptAsm(usize stackFrame);
}

#endif