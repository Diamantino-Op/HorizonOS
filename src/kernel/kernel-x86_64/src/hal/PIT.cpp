#include "PIT.hpp"

#include "CommonMain.hpp"
#include "IOPort.hpp"
#include "Interrupts.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace common::hal;

	usize PIT::ticks;

	void PIT::init(const isize freq) {
		const u16 div = frequency / freq;

		IOPort::out8(CMChannels::CHANNEL1 | CMAccess::LOBYTE | CMModes::SQUARE_WAVE, commandModePortAddress);
		IOPort::out8(div & 0xFF, channel0DataAddress);
		IOPort::out8((div >> 8) & 0xFF, channel0DataAddress);

		Interrupts::setHandler(0x20, reinterpret_cast<HandlerFun>(addTick), nullptr);

		Interrupts::unmask(0x20);

		this->clock = {
			.name = "PIT",
			.priority = 0,
			.getNs = &PIT::getNs,
		};

		CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
	}

	u16 PIT::readCount() {
		IOPort::out8(0x00, commandModePortAddress);

		const u8 low = IOPort::in8(channel0DataAddress);
		const u8 high = IOPort::in8(channel0DataAddress);

		return (high << 8) | low;
	}

	u64 PIT::getNs() {
		return ((ticks * 1'000) / frequency) * 1'000'000ul;
	}

	u32 PIT::addTick(u64 *) {
		ticks++;

		return 0;
	}
}