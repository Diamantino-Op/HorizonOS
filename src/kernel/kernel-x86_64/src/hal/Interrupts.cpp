#include "Interrupts.hpp"

namespace x86_64::hal {
	extern "C" void handleInterrupt(usize stackFrame) {
		Frame* frame = reinterpret_cast<Frame *>(stackFrame);


	}
}