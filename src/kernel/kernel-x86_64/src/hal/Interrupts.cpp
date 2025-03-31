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
	}

	void userPanic(Frame& frame) {
		Terminal* terminal = &Kernel::getTerminal();

		terminal->printf("%s------------------------------ Userland Panic ------------------------------\n", terminal->getFormat(terminal->getTextFormatting(TextFormatting::Regular), terminal->getTextColor(TextColor::Red)));
	}

	void backtrace(usize rbp) {

	}
}