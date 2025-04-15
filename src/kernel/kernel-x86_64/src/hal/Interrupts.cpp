#include "Interrupts.hpp"

#include "GDT.hpp"
#include "Main.hpp"

namespace kernel::x86_64::hal {
	extern "C" void handleInterruptAsm(usize stackFrame) {
		if (Frame &frame = *reinterpret_cast<Frame *>(stackFrame); frame.intNo < 32) {
			if (frame.cs == (Selector::USER_CODE * 8 | 3)) {
				userPanic(frame);
			} else {
				kernelPanic(frame);
			}
		}
	}

	void kernelPanic(Frame& frame) {
		Terminal* terminal = CommonMain::getTerminal();

		//TODO Move to own function
		u64 cr2Val = 0;
		u64 cr3Val = 0;

		asm volatile("mov %%cr2, %0" : "=r"(cr2Val));
		asm volatile("mov %%cr3, %0" : "=r"(cr3Val));

		terminal->printf("\033[0;31m------------------------------ Kernel Panic ------------------------------\n");
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Cause: %s\n", faultMessages[frame.intNo]);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   At: %s:%llu (%s)\n", __FILE__, __LINE__, __func__); // TODO: Fix this
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Registers:\n");
		terminal->printf("\033[0;31m-   int: 0x%.16lx\n", frame.intNo);
		terminal->printf("\033[0;31m-   err: 0x%.16lx\n", frame.errNo);
		terminal->printf("\033[0;31m-   rip: 0x%.16lx\n", frame.rip);
		terminal->printf("\033[0;31m-   rbp: 0x%.16lx\n", frame.rbp);
		terminal->printf("\033[0;31m-   rsp: 0x%.16lx\n", frame.rsp);
		terminal->printf("\033[0;31m-   cr2: 0x%.16lx\n", cr2Val);
		terminal->printf("\033[0;31m-   cr3: 0x%.16lx\n", cr3Val);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Backtrace:\n");
		backtrace(frame.rbp);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m--------------------------------------------------------------------------\n");

		for (;;) {
			asm volatile ("hlt");
		}
	}

	void userPanic(const Frame & frame) {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("\033[0;31m------------------------------ Userland Panic ------------------------------\n");
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Cause: %s\n", faultMessages[frame.intNo]);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Registers:\n");
		terminal->printf("\033[0;31m-   int: 0x%.16lx\n", frame.intNo);
		terminal->printf("\033[0;31m-   err: 0x%.16lx\n", frame.errNo);
		terminal->printf("\033[0;31m-   rip: 0x%.16lx\n", frame.rip);
		terminal->printf("\033[0;31m-   rbp: 0x%.16lx\n", frame.rbp);
		terminal->printf("\033[0;31m-   rsp: 0x%.16lx\n", frame.rsp);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m--------------------------------------------------------------------------\n");
	}

	void backtrace(usize rbp) {
		Terminal* terminal = CommonMain::getTerminal();

		usize* frame = reinterpret_cast<usize*>(rbp);

		while (frame) {
			const usize ip = frame[1];
			const usize sp = frame[0];

			terminal->printf("\033[0;31m-   ip: 0x%.16lx, sp: 0x%.16lx\n", ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}