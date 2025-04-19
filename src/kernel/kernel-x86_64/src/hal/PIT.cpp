#include "PIT.hpp"

#include "hal/IOPort.hpp"

namespace kernel::x86_64::hal {
	using namespace common::hal;

	void PIT::init(isize freq) {
		const u16 div = frequency / freq;

		IOPort::out8(CMChannels::CHANNEL1 | CMAccess::LOBYTE | CMModes::SQUARE_WAVE, commandModePortAddress);
		IOPort::out8(div & 0xFF, channel0DataAddress);
		IOPort::out8((div >> 8) & 0xFF, channel0DataAddress);
	}

	u16 PIT::readCount() {
		IOPort::out8(0x00, commandModePortAddress);

		const u8 low = IOPort::in8(channel0DataAddress);
		const u8 high = IOPort::in8(channel0DataAddress);

		return (high << 8) | low;
	}
}