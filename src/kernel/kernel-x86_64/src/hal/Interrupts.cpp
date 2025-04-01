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
		Terminal* terminal = &Kernel::getTerminal();

		terminal->printf("%s------------------------------ Kernel Panic ------------------------------\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   Cause: %s\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), faultMessages[frame.intNo]);
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   Registers:\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   int: 0x%lx\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.intNo);
		terminal->printf("%s-   err: 0x%lx\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.errNo);
		terminal->printf("%s-   rip: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rip);
		terminal->printf("%s-   rbp: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rbp);
		terminal->printf("%s-   rsp: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rsp);
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		backtrace(frame.rbp);
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s--------------------------------------------------------------------------\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
	}

	void userPanic(Frame& frame) {
		Terminal* terminal = &Kernel::getTerminal();

		terminal->printf("%s------------------------------ Userland Panic ------------------------------\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   Cause: %s\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), faultMessages[frame.intNo]);
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   Registers:\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s-   int: 0x%lx\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.intNo);
		terminal->printf("%s-   err: 0x%lx\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.errNo);
		terminal->printf("%s-   rip: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rip);
		terminal->printf("%s-   rbp: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rbp);
		terminal->printf("%s-   rsp: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), frame.rsp);
		terminal->printf("%s-\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
		terminal->printf("%s--------------------------------------------------------------------------\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
	}

	void backtrace(usize rbp) {
		Terminal* terminal = &Kernel::getTerminal();

		usize* frame = reinterpret_cast<usize*>(rbp);

		while (frame) {
			usize ip = frame[1];
			usize sp = frame[0];

			terminal->printf("%s-   ip: 0x%lp, sp: 0x%lp\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)), ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}