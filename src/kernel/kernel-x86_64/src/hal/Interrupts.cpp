#include "Interrupts.hpp"

namespace kernel::x86_64::hal {
	extern "C" void handleInterruptAsm(usize stackFrame) {
		Frame* frame = reinterpret_cast<Frame *>(stackFrame);


	}

	void backtrace(usize rbp) {

	}
}