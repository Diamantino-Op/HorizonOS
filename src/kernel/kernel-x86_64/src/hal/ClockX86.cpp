#include "hal/Clock.hpp"

#include "utils/Asm.hpp"
#include "Main.hpp"

namespace kernel::common::hal {
	using namespace x86_64;
	using namespace x86_64::utils;

	CalibratorFun Clocks::getCalibrator() {
		if (CommonMain::getInstance()->getAcpiPM()->supported()) {
			return AcpiPM::calibrate;
		}

		return calibrate;
	}

	void Clocks::archPause() {
		Asm::pause();
	}
}