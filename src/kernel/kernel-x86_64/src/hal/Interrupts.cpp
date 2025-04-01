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
		Terminal::printf("%s------------------------------ Kernel Panic ------------------------------\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   Cause: %s\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), faultMessages[frame.intNo]);
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   Registers:\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   int: 0x%lx\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.intNo);
		Terminal::printf("%s-   err: 0x%lx\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.errNo);
		Terminal::printf("%s-   rip: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rip);
		Terminal::printf("%s-   rbp: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rbp);
		Terminal::printf("%s-   rsp: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rsp);
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		backtrace(frame.rbp);
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s--------------------------------------------------------------------------\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
	}

	void userPanic(Frame& frame) {
		Terminal::printf("%s------------------------------ Userland Panic ------------------------------\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   Cause: %s\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), faultMessages[frame.intNo]);
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   Registers:\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s-   int: 0x%lx\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.intNo);
		Terminal::printf("%s-   err: 0x%lx\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.errNo);
		Terminal::printf("%s-   rip: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rip);
		Terminal::printf("%s-   rbp: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rbp);
		Terminal::printf("%s-   rsp: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), frame.rsp);
		Terminal::printf("%s-\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
		Terminal::printf("%s--------------------------------------------------------------------------\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)));
	}

	void backtrace(usize rbp) {
		usize* frame = reinterpret_cast<usize*>(rbp);

		while (frame) {
			usize ip = frame[1];
			usize sp = frame[0];

			Terminal::printf("%s-   ip: 0x%lp, sp: 0x%lp\n", Terminal::getFormat(Terminal::getTextFormatting(TextFormatting::Regular), Terminal::getTextColor(TextColor::Red)), ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}