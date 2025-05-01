#include "hal/Clock.hpp"

#include "utils/Asm.hpp"

namespace kernel::common::hal {
	using namespace x86_64::utils;

	void Clocks::archPause() {
		Asm::pause();
	}
}