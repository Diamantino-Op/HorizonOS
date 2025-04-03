#include "Interrupts.hpp"

#include "GDT.hpp"
#include "Main.hpp"

namespace kernel::x86_64::hal {
	extern "C" void handleInterruptAsm(usize stackFrame) {
		Frame& frame = *reinterpret_cast<Frame *>(stackFrame);

		if (frame.intNo < 32) {
			if (frame.cs == (Selector::USER_CODE * 8 | 3)) {
				userPanic(frame);
			} else {
				kernelPanic(frame);
			}
		}
	}

	void kernelPanic(Frame& frame) {
		Terminal* terminal = Kernel::getTerminal();

		terminal->printf("\033[0;31m------------------------------ Kernel Panic ------------------------------\n");
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Cause: %s\n", faultMessages[frame.intNo]);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Registers:\n");
		terminal->printf("\033[0;31m-   int: 0x%lx\n", frame.intNo);
		terminal->printf("\033[0;31m-   err: 0x%lx\n", frame.errNo);
		terminal->printf("\033[0;31m-   rip: 0x%lp\n", frame.rip);
		terminal->printf("\033[0;31m-   rbp: 0x%lp\n", frame.rbp);
		terminal->printf("\033[0;31m-   rsp: 0x%lp\n", frame.rsp);
		terminal->printf("\033[0;31m-\n");
		backtrace(frame.rbp);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m--------------------------------------------------------------------------\n");
	}

	void userPanic(const Frame & frame) {
		Terminal* terminal = Kernel::getTerminal();

		terminal->printf("\033[0;31m------------------------------ Userland Panic ------------------------------\n");
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Cause: %s\n", faultMessages[frame.intNo]);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m-   Registers:\n");
		terminal->printf("\033[0;31m-   int: 0x%lx\n", frame.intNo);
		terminal->printf("\033[0;31m-   err: 0x%lx\n", frame.errNo);
		terminal->printf("\033[0;31m-   rip: 0x%lp\n", frame.rip);
		terminal->printf("\033[0;31m-   rbp: 0x%lp\n", frame.rbp);
		terminal->printf("\033[0;31m-   rsp: 0x%lp\n", frame.rsp);
		terminal->printf("\033[0;31m-\n");
		terminal->printf("\033[0;31m--------------------------------------------------------------------------\n");
	}

	void backtrace(usize rbp) {
		Terminal* terminal = Kernel::getTerminal();

		usize* frame = reinterpret_cast<usize*>(rbp);

		while (frame) {
			const usize ip = frame[1];
			const usize sp = frame[0];

			terminal->printf("\033[0;31m-   ip: 0x%lp, sp: 0x%lp\n", ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}