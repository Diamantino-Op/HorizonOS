#include "Interrupts.hpp"

#include "GDT.hpp"
#include "Main.hpp"

namespace kernel::x86_64::hal {
	extern "C" void handleInterruptAsm(usize stackFrame) {
		if (Frame &frame = *reinterpret_cast<Frame *>(stackFrame); frame.intNo == 14) {
			handlePageFault(frame);
		} else if (frame.intNo < 32) {
			if (frame.cs == (Selector::USER_CODE * 8 | 3)) {
				userPanic(frame);
			} else {
				kernelPanic(frame);
			}
		}
	}

	// TODO: Fix
	void handlePageFault(Frame& frame) {
		kernelPanic(frame); // Remove after fix

		Terminal* terminal = CommonMain::getTerminal();

		u64 faultAddr = 0;
		asm volatile("mov %%cr2, %0" : "=r"(faultAddr));

		u8 flags = 0b00000011;

		if (frame.errNo & 0x4) { // User
			flags |= 0b00000100;
		}

		if (!(frame.errNo & 0x1)) { // Present
			u64 physAddress = reinterpret_cast<u64>(reinterpret_cast<Kernel *>(CommonMain::getInstance())->getPMM()->allocPages(1, false));

			reinterpret_cast<Kernel *>(CommonMain::getInstance())->getVMM()->mapPage(faultAddr, physAddress, flags, false);

			asm volatile("invlpg (%0)" ::"r" (faultAddr) : "memory");

			terminal->error("PageFault at address: 0x%.16lx", "Interrupts", faultAddr);
		} else {
			kernelPanic(frame);
		}
	}

	void kernelPanic(Frame& frame) {
		Terminal* terminal = CommonMain::getTerminal();

		//TODO Move to own function
		u64 cr2Val = 0;
		u64 cr3Val = 0;

		asm volatile("mov %%cr2, %0" : "=r"(cr2Val));
		asm volatile("mov %%cr3, %0" : "=r"(cr3Val));

		terminal->printf(true, "\033[0;31m------------------------------ Kernel Panic ------------------------------");
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Cause: %s", faultMessages[frame.intNo]);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   At: %s:%llu (%s)", __FILE__, __LINE__, __func__); // TODO: Fix this
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Registers:");
		terminal->printf(true, "\033[0;31m-   int: 0x%.16lx", frame.intNo);
		terminal->printf(true, "\033[0;31m-   err: 0x%.16lx", frame.errNo);
		terminal->printf(true, "\033[0;31m-   rip: 0x%.16lx", frame.rip);
		terminal->printf(true, "\033[0;31m-   rbp: 0x%.16lx", frame.rbp);
		terminal->printf(true, "\033[0;31m-   rsp: 0x%.16lx", frame.rsp);
		terminal->printf(true, "\033[0;31m-   cr2: 0x%.16lx", cr2Val);
		terminal->printf(true, "\033[0;31m-   cr3: 0x%.16lx", cr3Val);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Backtrace:");
		backtrace(frame.rbp);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m--------------------------------------------------------------------------");

		for (;;) {
			asm volatile ("hlt");
		}
	}

	void userPanic(const Frame & frame) {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf(true, "\033[0;31m------------------------------ Userland Panic ------------------------------");
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Cause: %s", faultMessages[frame.intNo]);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Registers:");
		terminal->printf(true, "\033[0;31m-   int: 0x%.16lx", frame.intNo);
		terminal->printf(true, "\033[0;31m-   err: 0x%.16lx", frame.errNo);
		terminal->printf(true, "\033[0;31m-   rip: 0x%.16lx", frame.rip);
		terminal->printf(true, "\033[0;31m-   rbp: 0x%.16lx", frame.rbp);
		terminal->printf(true, "\033[0;31m-   rsp: 0x%.16lx", frame.rsp);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m--------------------------------------------------------------------------");
	}

	void backtrace(usize rbp) {
		Terminal* terminal = CommonMain::getTerminal();

		usize* frame = reinterpret_cast<usize*>(rbp);

		while (frame) {
			const usize ip = frame[1];
			const usize sp = frame[0];

			terminal->printf(true, "\033[0;31m-   ip: 0x%.16lx, sp: 0x%.16lx", ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}