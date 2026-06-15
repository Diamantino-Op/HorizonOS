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

			if ((frame->cs & 0x3) == 3) {
				deliverPendingSignal(frame);
			}
		} else if (frame->intNo == 2) {
			Scheduler::isDisabled = true;

			CommonMain::getTerminal()->warnNoLock("NMI", "Interrupts");

			const bool prevIF = CommonMain::getTerminal()->lock();

			Scheduler::debugDump();

			CommonMain::getTerminal()->unlock(prevIF);

			Scheduler::isDisabled = false;
		} else if (frame->intNo < 32) {
			kernelPanic(frame);

			if ((frame->cs & 0x3) == 3) {
				deliverPendingSignal(frame);
			}
		} else if (frame->intNo == 0x80) {
			intSyscallEntry(frame);
		} else {
			const u8 savedIntNo = frame->intNo;

			CpuManager::getCurrentCore()->lastInt = frame->intNo;

			if (CpuManager::getCurrentCore() == nullptr or CpuManager::getCurrentCore()->interruptAllocator == nullptr) {
				sendEOI();

				return;
			}

			const IsrHandler *handler = CpuManager::getCurrentCore()->interruptAllocator->getHandler(savedIntNo);

			if (handler == nullptr or handler->fun == nullptr) {
				sendEOI();

				return;
			}

			const u32 ret = handler->fun(handler->ctx);

			if (ret == 0) {
				sendEOI();
			}
		}
	}

	void Interrupts::sendEOI() {
		/*if (CpuManager::getCurrentCore()->apic.isInitialized()) {
			CpuManager::getCurrentCore()->apic.eoi();
		} else {
			reinterpret_cast<Kernel *>(CommonMain::getInstance())->getDualPic()->eoi(intNo);
		}*/

		CpuManager::getCurrentCore()->apic.eoi();
	}

	// TODO: Fix
	void Interrupts::handlePageFault(Frame *frame) {
		kernelPanic(frame); // Remove after fix

		Terminal *terminal = CommonMain::getTerminal();

		const u64 faultAddr = Asm::readCr2();
		const u64 pageAddr = faultAddr & ~0xFFFULL;

		u8 flags = 0b00000011;

		if (frame->errNo & 0x4) { // User
			flags |= 0b00000100;
		}

		if (frame->errNo & 0x1) { // Present
			kernelPanic(frame);
			return;
		}

		const u64 physAddress = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

		if (physAddress == 0) {
			terminal->error("PageFault allocation failed at address: 0x%.16lx", "Interrupts", faultAddr);
			kernelPanic(frame);
			return;
		}

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(pageAddr, physAddress, flags, false, false);

		Asm::invalidatePage(pageAddr);

		terminal->error("PageFault at address: 0x%.16lx", "Interrupts", faultAddr);
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

		Scheduler::isDisabled = true;

		const bool prevIF = terminal->lock();

		const Thread *thread = Scheduler::getCurrentThread();

		u16 tid = 0;
		u16 pid = 0;

		if (thread != nullptr) {
			tid = thread->getId();
			pid = thread->getParent()->getId();

		}

		u64 offset = 0;
		u16 ds = 0;
		u16 es = 0;
		u16 fs = 0;
		u16 gs = 0;

		asm volatile("mov %%ds, %0" : "=rm"(ds));
		asm volatile("mov %%es, %0" : "=rm"(es));
		asm volatile("mov %%fs, %0" : "=rm"(fs));
		asm volatile("mov %%gs, %0" : "=rm"(gs));

		terminal->printfBoth(true, "\033[0;31m┌──────────────────────────[ Kernel Panic ]───────────────────────────");
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Cause: %s", faultMessages[frame->intNo]);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Registers:");
		terminal->printfBoth(true, "\033[0;31m│   tid: %u, pid: %u", tid, pid);
		terminal->printfBoth(true, "\033[0;31m│   int: %u, lastInt: %u, schedInt: %u, cpuId: %lu", frame->intNo, CpuManager::getCurrentCore()->lastInt, CpuManager::getCurrentCore()->schedInt, CpuManager::getCurrentCore()->cpuId);
		terminal->printfBoth(true, "\033[0;31m│   err: 0x%.16lx", frame->errNo);
		terminal->printfBoth(true, "\033[0;31m│   rip: 0x%.16lx (%s)", frame->rip, Profiler::findSymbol(frame->rip, &offset));
		terminal->printfBoth(true, "\033[0;31m│   rax: 0x%.16lx, rbx: 0x%.16lx, rcx: 0x%.16lx, rdx: 0x%.16lx", frame->rax, frame->rbx, frame->rcx, frame->rdx);
		terminal->printfBoth(true, "\033[0;31m│   rsi: 0x%.16lx, rdi: 0x%.16lx, r8:  0x%.16lx, r9:  0x%.16lx", frame->rsi, frame->rdi, frame->r8, frame->r9);
		terminal->printfBoth(true, "\033[0;31m│   r10: 0x%.16lx, r11: 0x%.16lx, r12: 0x%.16lx, r13: 0x%.16lx", frame->r10, frame->r11, frame->r12, frame->r13);
		terminal->printfBoth(true, "\033[0;31m│   r14: 0x%.16lx, r15: 0x%.16lx", frame->r14, frame->r15);
		terminal->printfBoth(true, "\033[0;31m│   rbp: 0x%.16lx", frame->rbp);
		terminal->printfBoth(true, "\033[0;31m│   rsp: 0x%.16lx", frame->rsp);
		terminal->printfBoth(true, "\033[0;31m│   cs:  0x%.16lx, ss:  0x%.16lx, rflags: 0x%.16lx", frame->cs, frame->ss, frame->rFlags);
		terminal->printfBoth(true, "\033[0;31m│   ds:  0x%.4x, es:  0x%.4x, fs:  0x%.4x, gs:  0x%.4x", ds, es, fs, gs);
		terminal->printfBoth(true, "\033[0;31m│   cr2: 0x%.16lx", Asm::readCr2());
		terminal->printfBoth(true, "\033[0;31m│   cr3: 0x%.16lx", Asm::readCr3());
		terminal->printfBoth(true, "\033[0;31m│   UGS: 0x%.16lx", Asm::rdmsr(UGSBAS));
		terminal->printfBoth(true, "\033[0;31m│   KGS: 0x%.16lx", Asm::rdmsr(KGSBAS));
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m│   Backtrace:");
		backtrace(frame->rbp, (frame->cs & 0x3) == 3);
		terminal->printfBoth(true, "\033[0;31m│");
		terminal->printfBoth(true, "\033[0;31m└──────────────────────────────────────────────────────────────────────");

		terminal->unlock(prevIF);

		//Scheduler::isDisabled = false;

		Asm::lhlt();
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

using namespace kernel::x86_64::hal;
using namespace kernel::common;

extern "C" void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
	Asm::cli();

	Terminal *terminal = CommonMain::getTerminal();

	Scheduler::isDisabled = true;

	const bool prevIF = terminal->lock();

	terminal->printfBoth(true, "\033[0;31mAssertion failed: %s\nFile: %s\nLine: %u\nFunction: %s", assertion, file, line, function);

	terminal->unlock(prevIF);

	Asm::lhlt();
}
