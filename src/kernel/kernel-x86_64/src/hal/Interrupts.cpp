#include "Interrupts.hpp"

#include "GDT.hpp"
#include "Main.hpp"

#include "utils/Asm.hpp"

namespace kernel::x86_64::hal {
	using namespace utils;

	IsrHandler Interrupts::handlers[224];

	extern "C" void handleInterruptAsm(const usize stackFrame) {
		const Frame &frame = *reinterpret_cast<Frame *>(stackFrame);

		Interrupts::handleInterrupt(frame);
	}

	void Interrupts::handleInterrupt(const Frame &frame) {
		if (frame.intNo == 14) {
			handlePageFault(frame);
		} else if (frame.intNo == 2) {
			Terminal *terminal = CommonMain::getTerminal();

			terminal->debug("NMI Received!", "Interrupts");
		} else if (frame.intNo < 32) {
			if (frame.cs == (Selector::USER_CODE * 8 | 3)) {
				userPanic(frame);
			} else {
				kernelPanic(frame);
			}
		} else if (const IsrHandler *handler = &handlers[frame.intNo - 32]; handler->fun) {
			handler->fun(handler->ctx);

			sendEOI(frame.intNo);
		}
	}

	void Interrupts::sendEOI(const usize intNo) {
		if (auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance()); kernel->getCpuManager()->getBootstrapCpu()->apic.isInitialized()) {

		} else {
			kernel->getDualPic()->eoi(intNo);
		}
	}

	// TODO: Fix
	void Interrupts::handlePageFault(const Frame &frame) {
		kernelPanic(frame); // Remove after fix

		Terminal *terminal = CommonMain::getTerminal();

		const u64 faultAddr = Asm::readCr2();

		u8 flags = 0b00000011;

		if (frame.errNo & 0x4) { // User
			flags |= 0b00000100;
		}

		if (!(frame.errNo & 0x1)) { // Present
			const u64 physAddress = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

			CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(faultAddr, physAddress, flags, false, false);

			Asm::invalidatePage(faultAddr);

			terminal->error("PageFault at address: 0x%.16lx", "Interrupts", faultAddr);
		} else {
			kernelPanic(frame);
		}
	}

	void Interrupts::setHandler(const u8 id, u64 *handler, u64 *ctx) {
		handlers[id - 0x20].fun = reinterpret_cast<HandlerFun>(handler);
		handlers[id - 0x20].ctx = ctx;
	}

	void Interrupts::setHandler(const u8 id, const HandlerFun handler, u64 *ctx) {
		handlers[id - 0x20].fun = handler;
		handlers[id - 0x20].ctx = ctx;
	}

	IsrHandler *Interrupts::getHandler(const u8 id) {
		return &handlers[id - 0x20];
	}

	void Interrupts::mask(const u8 id) {
		if (auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance()); kernel->getCpuManager()->getBootstrapCpu()->apic.isInitialized()) {

		} else {
			kernel->getDualPic()->mask(id);
		}
	}

	void Interrupts::unmask(const u8 id) {
		if (auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance()); kernel->getCpuManager()->getBootstrapCpu()->apic.isInitialized()) {

		} else {
			kernel->getDualPic()->unmask(id);
		}
	}

	void Interrupts::kernelPanic(const Frame &frame) {
		Terminal * terminal = CommonMain::getTerminal();

		terminal->printf(true, "\033[0;31m------------------------------ Kernel Panic ------------------------------");
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Cause: %s", faultMessages[frame.intNo]);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   At: %s:%llu (%s)", __FILE__, __LINE__, __func__); // TODO: Fix this
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Registers:");
		terminal->printf(true, "\033[0;31m-   int: 0x%.16lx", frame.intNo);
		terminal->printf(true, "\033[0;31m-   err: 0x%.16lx", frame.errNo);
		terminal->printf(true, "\033[0;31m-   rip: 0x%.16lx", frame.rip);
		terminal->printf(true, "\033[0;31m-   rbp: 0x%.16lx", frame.rbp);
		terminal->printf(true, "\033[0;31m-   rsp: 0x%.16lx", frame.rsp);
		terminal->printf(true, "\033[0;31m-   cr2: 0x%.16lx", Asm::readCr2());
		terminal->printf(true, "\033[0;31m-   cr3: 0x%.16lx", Asm::readCr3());
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Backtrace:");
		backtrace(frame.rbp);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m--------------------------------------------------------------------------");

		Asm::lhlt();
	}

	void Interrupts::userPanic(const Frame &frame) {
		Terminal * terminal = CommonMain::getTerminal();

		terminal->printf(true, "\033[0;31m------------------------------ Userland Panic ------------------------------");
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Cause: %s", faultMessages[frame.intNo]);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m-   Registers:");
		terminal->printf(true, "\033[0;31m-   int: 0x%.16lx", frame.intNo);
		terminal->printf(true, "\033[0;31m-   err: 0x%.16lx", frame.errNo);
		terminal->printf(true, "\033[0;31m-   rip: 0x%.16lx", frame.rip);
		terminal->printf(true, "\033[0;31m-   rbp: 0x%.16lx", frame.rbp);
		terminal->printf(true, "\033[0;31m-   rsp: 0x%.16lx", frame.rsp);
		terminal->printf(true, "\033[0;31m-");
		terminal->printf(true, "\033[0;31m--------------------------------------------------------------------------");
	}

	void Interrupts::backtrace(const usize rbp) {
		Terminal *terminal = CommonMain::getTerminal();

		const auto *frame = reinterpret_cast<usize *>(rbp);

		while (frame) {
			const usize ip = frame[1];
			const usize sp = frame[0];

			terminal->printf(true, "\033[0;31m-   ip: 0x%.16lx, sp: 0x%.16lx", ip, sp);

			frame = reinterpret_cast<usize*>(sp);
		}
	}
}