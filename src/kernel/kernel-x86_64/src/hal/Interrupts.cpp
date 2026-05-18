#include "Interrupts.hpp"

#include "GDT.hpp"
#include "Main.hpp"

#include "utils/Asm.hpp"
#include "hal/SyscallX86.hpp"

namespace kernel::x86_64::hal {
	using namespace utils;

	constexpr usize maxBacktraceFrames = 64;

	extern "C" void handleInterruptAsm(const usize stackFrame) {
		auto *frame = reinterpret_cast<Frame *>(stackFrame);

		Interrupts::handleInterrupt(frame);
	}

	void Interrupts::handleInterrupt(Frame *frame) {
		if (frame->intNo == 14) {
			handlePageFault(frame);
		} else if (frame->intNo == 2) {
			Terminal *terminal = CommonMain::getTerminal();

			terminal->debug("NMI Received!", "Interrupts");
		} else if (frame->intNo < 32) {
			if (frame->cs == (Selector::USER_CODE64 * 8 | 3) or frame->cs == (Selector::USER_CODE32 * 8 | 3)) {
				userPanic(frame);
			} else {
				kernelPanic(frame);
			}
		} else if (frame->intNo == 0x80) {
			intSyscallEntry(frame);
		} else if (const IsrHandler *handler = CpuManager::getCurrentCore()->interruptAllocator->getHandler(frame->intNo); handler != nullptr and handler->fun != nullptr) {
			handler->fun(handler->ctx);

			if ((frame->cs & 0x3) == 3) {
				deliverPendingSignal(frame);
			}

			sendEOI(frame->intNo);
		}
	}

	void Interrupts::sendEOI(const usize intNo) {
		if (CpuManager::getCurrentCore()->apic.isInitialized()) {
			CpuManager::getCurrentCore()->apic.eoi();
		} else {
			reinterpret_cast<Kernel *>(CommonMain::getInstance())->getDualPic()->eoi(intNo);
		}
	}

	// TODO: Fix
	void Interrupts::handlePageFault(Frame *frame) {
		kernelPanic(frame); // Remove after fix

		Terminal *terminal = CommonMain::getTerminal();

		const u64 faultAddr = Asm::readCr2();

		u8 flags = 0b00000011;

		if (frame->errNo & 0x4) { // User
			flags |= 0b00000100;
		}

		if (not (frame->errNo & 0x1)) { // Present
			const u64 physAddress = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

			CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(faultAddr, physAddress, flags, false, false);

			Asm::invalidatePage(faultAddr);

			terminal->error("PageFault at address: 0x%.16lx", "Interrupts", faultAddr);
		} else {
			kernelPanic(frame);
		}
	}

	void Interrupts::mask(const u8 id) {
		if (auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance()); kernel->getIOApicManager()->isInitialized()) {
			kernel->getIOApicManager()->mask(id);
		} else {
			kernel->getDualPic()->mask(id);
		}
	}

	void Interrupts::unmask(const u8 id) {
		if (auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance()); kernel->getCpuManager()->getBootstrapCpu() != nullptr and kernel->getCpuManager()->getBootstrapCpu()->apic.isInitialized()) {
			kernel->getIOApicManager()->unmask(id);
		} else {
			kernel->getDualPic()->unmask(id);
		}
	}

	void Interrupts::kernelPanic(Frame *frame) {
		Asm::cli();

		Terminal *terminal = CommonMain::getTerminal();

		const bool prevIF = terminal->lock();

		u64 offset = 0;

		terminal->printfBoth(true, "\033[0;31m┌──────────────────────────[ Kernel Panic ]───────────────────────────");
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Cause: %s", faultMessages[frame->intNo]);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Registers:");
		terminal->printfBoth(true, "\033[0;31m│   int: %u", frame->intNo);
		terminal->printfBoth(true, "\033[0;31m│   err: 0x%.16lx", frame->errNo);
		terminal->printfBoth(true, "\033[0;31m│   rip: 0x%.16lx (%s)", frame->rip, Profiler::findSymbol(frame->rip, &offset));
		terminal->printfBoth(true, "\033[0;31m│   rbp: 0x%.16lx", frame->rbp);
		terminal->printfBoth(true, "\033[0;31m│   rsp: 0x%.16lx", frame->rsp);
		terminal->printfBoth(true, "\033[0;31m│   cr2: 0x%.16lx", Asm::readCr2());
		terminal->printfBoth(true, "\033[0;31m│   cr3: 0x%.16lx", Asm::readCr3());
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Backtrace:");
		backtrace(frame->rbp, (frame->cs & 0x3) == 3);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m└──────────────────────────────────────────────────────────────────────");

		terminal->unlock(prevIF);

		Asm::lhlt();
	}

	void Interrupts::userPanic(Frame *frame) {
		Terminal *terminal = CommonMain::getTerminal();

		const bool prevIF = terminal->lock();

		u64 offset = 0;

		terminal->printfBoth(true, "\033[0;31m┌──────────────────────────[ Userland Panic ]───────────────────────────");
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Cause: %s", faultMessages[frame->intNo]);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Registers:");
		terminal->printfBoth(true, "\033[0;31m│   int: 0x%.16lx", frame->intNo);
		terminal->printfBoth(true, "\033[0;31m│   err: 0x%.16lx", frame->errNo);
		terminal->printfBoth(true, "\033[0;31m│   rip: 0x%.16lx (%s)", frame->rip, Profiler::findSymbol(frame->rip, &offset));
		terminal->printfBoth(true, "\033[0;31m│   rbp: 0x%.16lx", frame->rbp);
		terminal->printfBoth(true, "\033[0;31m│   rsp: 0x%.16lx", frame->rsp);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m└──────────────────────────────────────────────────────────────────────");

		terminal->unlock(prevIF);
	}

	void Interrupts::backtrace(const usize rbp, bool userMode) {
		Terminal *terminal = CommonMain::getTerminal();
		const auto *main = CommonMain::getInstance();
		const auto *scheduler = main != nullptr ? main->getScheduler() : nullptr;
		const auto *currentThread = scheduler != nullptr ? Scheduler::getCurrentThread() : nullptr;
		const auto *process = currentThread != nullptr ? currentThread->getParent() : nullptr;
		const auto *pageMapContext = process != nullptr ? process->getProcessContext() : (main != nullptr ? main->getKernelAllocContext() : nullptr);
		const usize stackTop = main != nullptr ? main->getStackTop() : 0;
		const usize stackBottom = stackTop > kernelStackSize ? stackTop - kernelStackSize : 0;
		const auto isValidBacktraceFrame = [pageMapContext, userMode, stackBottom, stackTop](const usize frameRbp) -> bool {
			if (frameRbp == 0 || (frameRbp & (alignof(usize) - 1)) != 0) {
				return false;
			}

			if (pageMapContext == nullptr) {
				return true;
			}

			if (!userMode && stackTop != 0 && (frameRbp < stackBottom || frameRbp + (sizeof(usize) * 2) > stackTop)) {
				return false;
			}

			const auto &pageMap = pageMapContext->pageMap;
			return pageMap.getPhysAddress(frameRbp) != 0 && pageMap.getPhysAddress(frameRbp + sizeof(usize)) != 0;
		};

		usize currentRbp = rbp;
		usize framesPrinted = 0;

		while (framesPrinted < maxBacktraceFrames && isValidBacktraceFrame(currentRbp)) {
			u64 offset = 0;

			const auto *frame = reinterpret_cast<const usize *>(currentRbp);
			const usize nextRbp = frame[0];
			const usize returnIp = frame[1];

			terminal->printfBoth(true, "\033[0;31m│   rip: 0x%.16lx (%s), rsp: 0x%.16lx", returnIp, Profiler::findSymbol(returnIp, &offset), nextRbp);

			if (nextRbp == 0 || nextRbp <= currentRbp) {
				return;
			}

			currentRbp = nextRbp;
			framesPrinted++;
		}
	}
}

