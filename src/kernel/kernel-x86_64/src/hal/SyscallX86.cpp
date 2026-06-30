#include "SyscallX86.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "GDT.hpp"
#include "Main.hpp"
#include "threading/PortMessaging.hpp"
#include "threading/SchedulerX86.hpp"
#include "uacpi/utilities.h"
#include "utils/Asm.hpp"

namespace kernel::common::hal {
	using namespace kernel::common::threading;
	using namespace x86_64;
	using namespace x86_64::hal;
	using namespace x86_64::utils;
	using namespace x86_64::threading;

	namespace {
		auto getThreadTss(Thread *thread) -> TssIopb * {
			if (thread == nullptr || thread->getContext() == nullptr) {
				return nullptr;
			}

			auto *ctx = reinterpret_cast<ThreadContext *>(thread->getContext());

			return ctx->threadTssIopb;
		}

		auto findProcessTssWithIopb(Process *process, const Thread *skipThread = nullptr) -> TssIopb * {
			if (process == nullptr) {
				return nullptr;
			}

			for (auto &thread : process->threadList) {
				if (&thread == skipThread) {
					continue;
				}

				if (TssIopb *threadTss = getThreadTss(&thread); threadTss != nullptr) {
					return threadTss;
				}
			}

			return nullptr;
		}

		auto ensureCurrentThreadTss(Thread *thread) -> TssIopb * {
			if (thread == nullptr || thread->getParent() == nullptr) {
				return nullptr;
			}

			auto *ctx = reinterpret_cast<ThreadContext *>(thread->getContext());

			if (ctx == nullptr) {
				return nullptr;
			}

			if (ctx->threadTssIopb == nullptr) {
				auto *threadTss = reinterpret_cast<TssIopb *>(VirtualAllocator::alloc(thread->getParent()->getProcessContext(), sizeof(TssIopb)));

				if (threadTss == nullptr) {
					return nullptr;
				}

				*threadTss = TssIopb();

				if (const TssIopb *sourceTss = findProcessTssWithIopb(thread->getParent(), thread); sourceTss != nullptr) {
					memcpy(threadTss->iopb, sourceTss->iopb, sizeof(threadTss->iopb));
				}

				ctx->threadTssIopb = threadTss;
			}

			ctx->updateTssPtrs(CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0]);

			CpuManager::getCurrentCore()->gdtManager->getGdt()->tssEntry = GdtTssEntry(ctx->threadTssIopb);

			return ctx->threadTssIopb;
		}

		auto applyIopermRange(TssIopb *threadTss, const u64 from, const u64 num, const u64 state) -> void {
			if (threadTss == nullptr) {
				return;
			}

			for (u64 i = 0; i < num; i++) {
				const u32 port = from + i;

				if (port > 0xFFFF) {
					break;
				}

				const u32 byteIdx = port / 8;
				const u8 bitIdx  = port % 8;

				if (state != 0) {
					threadTss->iopb[byteIdx] &= ~(1U << bitIdx);
				} else {
					threadTss->iopb[byteIdx] |=  (1U << bitIdx);
				}
			}
		}

		auto seedThreadIopb(ThreadContext *ctx, AllocContext *processContext, const TssIopb *sourceTss) -> bool {
			if (ctx == nullptr || processContext == nullptr || sourceTss == nullptr) {
				return false;
			}

			if (ctx->threadTssIopb == nullptr) {
				ctx->threadTssIopb = reinterpret_cast<TssIopb *>(VirtualAllocator::alloc(processContext, sizeof(TssIopb)));

				if (ctx->threadTssIopb == nullptr) {
					return false;
				}

				*ctx->threadTssIopb = TssIopb();
			}

			memcpy(ctx->threadTssIopb->iopb, sourceTss->iopb, sizeof(ctx->threadTssIopb->iopb));

			return true;
		}

		auto propagateProcessIopb(Process *process, const TssIopb *sourceTss) -> void {
			if (process == nullptr || sourceTss == nullptr) {
				return;
			}

			for (auto &thread : process->threadList) {
				auto *ctx = reinterpret_cast<ThreadContext *>(thread.getContext());

				if (ctx == nullptr || ctx->threadTssIopb == sourceTss) {
					continue;
				}

				if (ctx->threadTssIopb != nullptr) {
					seedThreadIopb(ctx, process->getProcessContext(), sourceTss);
				}
			}
		}

		auto propagateProcessIopermRange(Process *process, const TssIopb *sourceTss, const u64 from, const u64 num, const u64 state) -> void {
			if (process == nullptr || sourceTss == nullptr) {
				return;
			}

			for (auto &thread : process->threadList) {
				auto *ctx = reinterpret_cast<ThreadContext *>(thread.getContext());

				if (ctx == nullptr || ctx->threadTssIopb == sourceTss) {
					continue;
				}

				if (ctx->threadTssIopb != nullptr) {
					applyIopermRange(ctx->threadTssIopb, from, num, state);
				}
			}
		}

		auto isSignalIgnored(const u64 handler) -> bool {
			return handler == 1;
		}

		auto isSignalDefault(const u64 handler) -> bool {
			return handler == 0;
		}

		auto saveFrame(Thread *thread, const Frame *frame) -> void {
			SignalContext signalFrame {};

			signalFrame.rax = frame->rax;
			signalFrame.rbx = frame->rbx;
			signalFrame.rcx = frame->rcx;
			signalFrame.rdx = frame->rdx;
			signalFrame.rsi = frame->rsi;
			signalFrame.rdi = frame->rdi;
			signalFrame.r8 = frame->r8;
			signalFrame.r9 = frame->r9;
			signalFrame.r10 = frame->r10;
			signalFrame.r11 = frame->r11;
			signalFrame.r12 = frame->r12;
			signalFrame.r13 = frame->r13;
			signalFrame.r14 = frame->r14;
			signalFrame.r15 = frame->r15;
			signalFrame.rip = frame->rip;
			signalFrame.rFlags = frame->rFlags;
			signalFrame.rsp = frame->rsp;
			signalFrame.cs = frame->cs;
			signalFrame.ss = frame->ss;

			thread->setSignalFrame(signalFrame);
		}

		auto saveRegs(Thread *thread, const SyscallRegs *regs) -> void {
			SignalContext signalFrame {};

			signalFrame.rax = regs->rax;
			signalFrame.rbx = regs->rbx;
			signalFrame.rcx = regs->rcx;
			signalFrame.rdx = regs->rdx;
			signalFrame.rsi = regs->rsi;
			signalFrame.rdi = regs->rdi;
			signalFrame.r8 = regs->r8;
			signalFrame.r9 = regs->r9;
			signalFrame.r10 = regs->r10;
			signalFrame.r11 = regs->r11;
			signalFrame.r12 = regs->r12;
			signalFrame.r13 = regs->r13;
			signalFrame.r14 = regs->r14;
			signalFrame.r15 = regs->r15;
			signalFrame.rip = regs->rcx;
			signalFrame.rFlags = regs->r11;
			signalFrame.rsp = reinterpret_cast<u64>(regs) + sizeof(SyscallRegs);

			thread->setSignalFrame(signalFrame);
		}

		auto restoreRegs(const SignalContext &signalFrame, SyscallRegs *regs) -> void {
			regs->rax = signalFrame.rax;
			regs->rbx = signalFrame.rbx;
			regs->rcx = signalFrame.rip;
			regs->rdx = signalFrame.rdx;
			regs->rsi = signalFrame.rsi;
			regs->rdi = signalFrame.rdi;
			regs->r8 = signalFrame.r8;
			regs->r9 = signalFrame.r9;
			regs->r10 = signalFrame.r10;
			regs->r11 = signalFrame.rFlags;
			regs->r12 = signalFrame.r12;
			regs->r13 = signalFrame.r13;
			regs->r14 = signalFrame.r14;
			regs->r15 = signalFrame.r15;
		}

		auto setRestorerOnUserStack(u64 *stackPointer, const u64 restorer) -> void {
			if (stackPointer == nullptr) {
				return;
			}

			*stackPointer = restorer;
		}

		auto deliverSignal(Thread *thread, Frame *frame) -> bool {
			if (thread == nullptr || frame == nullptr || !thread->hasPendingSignal() || thread->hasSignalFrame()) {
				return false;
			}

			auto *process = thread->getParent();

			if (process == nullptr) {
				return false;
			}

			const u64 sig = thread->getPendingSignal();

			if (sig == 0 || sig > signalActionCount) {
				thread->clearSignalState();
				return false;
			}

			const SignalAction action = process->signalActions[sig - 1];

			if (isSignalIgnored(action.handler)) {
				thread->clearSignalState();
				return false;
			}

			if (isSignalDefault(action.handler)) {
				thread->clearSignalState();
				CommonMain::getInstance()->getScheduler()->killProcess(process);
				return true;
			}

			saveFrame(thread, frame);
			thread->clearPendingSignal();

			setRestorerOnUserStack(reinterpret_cast<u64 *>(frame->rsp), action.restorer);
			frame->rip = action.handler;
			frame->rdi = sig;
			frame->rsi = 0;
			frame->rdx = 0;

			return true;
		}

		auto deliverSignal(Thread *thread, SyscallRegs *regs) -> bool {
			if (thread == nullptr || regs == nullptr || !thread->hasPendingSignal() || thread->hasSignalFrame()) {
				return false;
			}

			auto *process = thread->getParent();

			if (process == nullptr) {
				return false;
			}

			const u64 sig = thread->getPendingSignal();

			if (sig == 0 || sig > signalActionCount) {
				thread->clearSignalState();
				return false;
			}

			const SignalAction action = process->signalActions[sig - 1];

			if (isSignalIgnored(action.handler)) {
				thread->clearSignalState();
				return false;
			}

			if (isSignalDefault(action.handler)) {
				thread->clearSignalState();
				CommonMain::getInstance()->getScheduler()->killProcess(process);
				return true;
			}

			saveRegs(thread, regs);
			thread->clearPendingSignal();

			setRestorerOnUserStack(reinterpret_cast<u64 *>(reinterpret_cast<u64>(regs) + sizeof(SyscallRegs)), action.restorer);
			regs->rcx = action.handler;
			regs->rdi = sig;
			regs->rsi = 0;
			regs->rdx = 0;

			return true;
		}
	}

	void SyscallManager::initArch() {
		constexpr u64 star = static_cast<u64>(Selector::USER_CODE32) << 48 | static_cast<u64>(Selector::KERNEL_CODE) << 32;

		Asm::wrmsr(Msrs::STAR, star);
		Asm::wrmsr(Msrs::LSTAR, reinterpret_cast<u64>(&syscallHandler));
		Asm::wrmsr(Msrs::FMASK, static_cast<u32>(~0x2));

		u64 efer = Asm::rdmsr(Msrs::EFER);

		efer |= (1 << 0);
		// efer |= (1 << 12); // SVME
		// efer |= (1 << 15); // TCE

		Asm::wrmsr(Msrs::EFER, efer);
	}

	auto SyscallManager::userIrqHandler(u64 *ctx) -> u32 {
		const auto *irq = reinterpret_cast<const IrqRegistration *>(ctx);

		auto notifyMsg = MessageHeader();

		auto notifyData = IrqReceiveData();

		notifyData.irqNum = irq->irq;
		notifyData.isIrq = irq->isIrq;
		notifyData.cpuId = CpuManager::getCurrentCore()->cpuId;

		notifyMsg.type = irqReceiveMsgType;
		notifyMsg.port = irq->port;

		notifyMsg.buffer = reinterpret_cast<u64 *>(&notifyData);
		notifyMsg.length = sizeof(IrqReceiveData);

		PortMessaging::sendMessage(0, irq->port, &notifyMsg);

		return 0;
	}

	void SyscallManager::setGsBase(const u64 gsBase) {
		Asm::wrmsr(Msrs::UGSBAS, gsBase);
	}

	void SyscallManager::setFsBase(const u64 fsBase) {
		Asm::wrmsr(Msrs::FSBAS, fsBase);
	}

	auto SyscallManager::getGsBase() -> u64 {
		return Asm::rdmsr(Msrs::UGSBAS);
	}

	auto SyscallManager::getFsBase() -> u64 {
		return Asm::rdmsr(Msrs::FSBAS);
	}

	auto SyscallManager::syscallGetCpu(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		const auto *core = CpuManager::getCurrentCore();
		*ret = core != nullptr ? static_cast<long>(core->cpuId) : 0;

		return 0;
	}

	auto SyscallManager::syscallIsThreadAlive(long *ret, const u64 tid, u64, u64, u64, u64, u64) -> u64 {
		if (ret != nullptr) {
			*ret = 0;
		}

		if (tid == 0 or tid > maxThreads) {
			return EINVAL;
		}

		Scheduler *sched = CommonMain::getInstance()->getScheduler();

		const bool prevIF = sched->getSchedLock()->lock();

		for (const auto &process : sched->processList) {
			for (const auto &thread : process.threadList) {
				if (thread.getId() == tid) {
					sched->getSchedLock()->unlock(prevIF);

					if (ret != nullptr) {
						*ret = 1;
					}

					return 0;
				}
			}
		}

		sched->getSchedLock()->unlock(prevIF);

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();
		const CpuCore *bspCore = cpuManager->getBootstrapCpu();

		const Thread *bspEntry = bspCore->executionNode.getCurrentThread();

		if (bspEntry != nullptr and bspEntry->getId() == tid) {
			if (ret != nullptr) {
				*ret = 1;
			}

			return 0;
		}

		const CoreKernel *coreList = cpuManager->getCoreList();
		const u64 cores = cpuManager->getCoreAmount();

		if (coreList != nullptr and cores > 1) {
			for (u64 i = 0; i < cores - 1; i++) {
				const CpuCore *core = &coreList[i].cpuCore;
				const Thread *entry = core->executionNode.getCurrentThread();

				if (entry != nullptr and entry->getId() == tid) {
					if (ret != nullptr) {
						*ret = 1;
					}

					return 0;
				}
			}
		}

		return 0;
	}

	auto SyscallManager::syscallIoPerm(long */*unused*/, const u64 from, const u64 num, const u64 state, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (from > 0xFFFF or num == 0 or from + num > 0x10000) {
			return EINVAL;
		}

		Thread *thread = Scheduler::getCurrentThread();

		if (thread == nullptr) {
			return EINVAL;
		}

		// TODO
		/*Process *proc = thread->getParent();
		if (proc == nullptr || !proc->isPrivileged()) {
			return EPERM;
		}*/

		TssIopb *threadTss = ensureCurrentThreadTss(thread);

		if (threadTss == nullptr) {
			return ENOMEM;
		}

		applyIopermRange(threadTss, from, num, state);

		propagateProcessIopermRange(thread->getParent(), threadTss, from, num, state);

		CpuManager::getCurrentCore()->gdtManager->getGdt()->tssEntry.clearBusy();

		TssManager::updateTss();

		return 0;
	}

	auto SyscallManager::syscallIoPl(long *ret, const u64 level, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret != nullptr) {
			*ret = 0;
		}

		if (level > 3) {
			return EINVAL;
		}

		Thread *thread = Scheduler::getCurrentThread();

		if (thread == nullptr) {
			return EINVAL;
		}

		// TODO
		/*Process *proc = thread->getParent();
		if (proc == nullptr || !proc->isPrivileged()) {
			return EPERM;
		}*/

		TssIopb *threadTss = ensureCurrentThreadTss(thread);

		if (threadTss == nullptr) {
			return ENOMEM;
		}

		memset(threadTss->iopb, level == 3 ? 0 : 0xFF, sizeof(threadTss->iopb));

		propagateProcessIopb(thread->getParent(), threadTss);

		CpuManager::getCurrentCore()->gdtManager->getGdt()->tssEntry.clearBusy();

		TssManager::updateTss();

		return 0;
	}

	auto SyscallManager::syscallInstallIRQHandler(long */*unused*/, const u64 irq, const u64 port, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		IrqAllocator *irqAllocator = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIrqAllocator();

		auto *registration = new IrqRegistration();

		registration->irq = irq;
		registration->isIrq = true;
		registration->port = port;

		if (irqRegistrations.addStart(registration) == nullptr) {
			delete registration;

			return UACPI_STATUS_INTERNAL_ERROR;
		}

		const u8 retInt = irqAllocator->allocateIrq(irq, 0, 0, IOApicDelivery::FIXED, &userIrqHandler, reinterpret_cast<u64 *>(registration));

		if (retInt == 0) {
			return UACPI_STATUS_ALREADY_EXISTS;
		}

		return UACPI_STATUS_OK;
	}

	auto SyscallManager::syscallUninstallIRQHandler(long */*unused*/, const u64 irq, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		IrqAllocator *irqAllocator = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIrqAllocator();

		IrqRegistration *selected = nullptr;

		auto it = irqRegistrations.begin();
		const auto end = irqRegistrations.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.irq == irq) {
				selected = &currEntry;
			}

			it = nextIt;
		}

		if (selected == nullptr) {
			return UACPI_STATUS_NOT_FOUND;
		}

		if (not irqRegistrations.remove(selected)) {
			return UACPI_STATUS_INTERNAL_ERROR;
		}

		if (not irqAllocator->freeIrq(irq, 0)) {
			return UACPI_STATUS_INTERNAL_ERROR;
		}

		return UACPI_STATUS_OK;
	}

	auto SyscallManager::syscallGetIRQMode(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		*ret = CpuManager::getCurrentCore()->apic.isInitialized() ?  1 : 0;

		return 0;
	}

	auto SyscallManager::syscallSetIntStatus(long *ret, const u64 status, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret != nullptr) {
			*ret = 0;
		}

		if (static_cast<bool>(status)) {
			Asm::sti();
		} else {
			Asm::cli();
		}

		return 0;
	}

	// TODO: destCpu is currently passed as CPU ID, so it might not work like this
	auto SyscallManager::syscallAllocIntVec(long *ret, const u64 port, const u64 destCpu, const u64 isLapic, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu >= cpuManager->getCoreAmount()) {
			return EFAULT;
		}

		const CpuCore *destCore = nullptr;

		if (static_cast<bool>(isLapic)) {
			if (cpuManager->getBootstrapCpu()->lapicId == destCpu) {
				destCore = cpuManager->getBootstrapCpu();
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.lapicId == destCpu) {
						destCore = &cpuManager->getCoreList()[i].cpuCore;
						break;
					}
				}
			}
		} else {
			if (cpuManager->getBootstrapCpu()->cpuId == destCpu) {
				destCore = cpuManager->getBootstrapCpu();
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.cpuId == destCpu) {
						destCore = &cpuManager->getCoreList()[i].cpuCore;
						break;
					}
				}
			}
		}

		if (destCore == nullptr) {
			return EFAULT;
		}

		auto *registration = new IrqRegistration();

		registration->port = port;

		if (irqRegistrations.addStart(registration) == nullptr) {
			irqRegistrations.remove(registration);

			return EFAULT;
		}

		const u8 intVec = destCore->interruptAllocator->allocInt(&userIrqHandler, reinterpret_cast<u64 *>(registration));

		registration->irq = intVec;
		registration->isIrq = false;

		*ret = static_cast<long>(intVec);

		if (intVec == 0) {
			return EFAULT;
		}

		return 0;
	}

	auto SyscallManager::syscallFreeIntVec(long */*unused*/, const u64 vec, const u64 destCpu, const u64 isLapic, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu >= cpuManager->getCoreAmount()) {
			return EFAULT;
		}

		const CpuCore *destCore = nullptr;

		if (static_cast<bool>(isLapic)) {
			if (cpuManager->getBootstrapCpu()->lapicId == destCpu) {
				destCore = cpuManager->getBootstrapCpu();
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.lapicId == destCpu) {
						destCore = &cpuManager->getCoreList()[i].cpuCore;
						break;
					}
				}
			}
		} else {
			if (cpuManager->getBootstrapCpu()->cpuId == destCpu) {
				destCore = cpuManager->getBootstrapCpu();
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.cpuId == destCpu) {
						destCore = &cpuManager->getCoreList()[i].cpuCore;
						break;
					}
				}
			}
		}

		if (destCore == nullptr or not destCore->interruptAllocator->freeInt(vec)) {
			return EFAULT;
		}

		auto it = irqRegistrations.begin();
		const auto end = irqRegistrations.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.irq == vec and not currEntry.isIrq) {
				irqRegistrations.remove(&currEntry);

				break;
			}

			it = nextIt;
		}

		return 0;
	}

	auto SyscallManager::syscallAllocGsi(long *ret, const u64 port, const u64 destCpu, const u64 isLapic, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		IrqAllocator *irqAllocator = kernel->getIrqAllocator();
		const CpuManager *cpuManager = kernel->getCpuManager();

		uint64_t gsiDestCpu = destCpu;

		if (static_cast<bool>(isLapic)) {
			if (cpuManager->getBootstrapCpu()->lapicId == destCpu) {
				gsiDestCpu = cpuManager->getBootstrapCpu()->cpuId;
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.lapicId == destCpu) {
						gsiDestCpu = cpuManager->getCoreList()[i].cpuCore.cpuId;

						break;
					}
				}
			}
		}

		auto *registration = new IrqRegistration();

		registration->port = port;

		if (irqRegistrations.addStart(registration) == nullptr) {
			irqRegistrations.remove(registration);

			return EFAULT;
		}

		const u64 gsi = irqAllocator->allocGsi(gsiDestCpu, 0, IOApicDelivery::FIXED, &userIrqHandler, reinterpret_cast<u64 *>(registration));

		registration->irq = gsi;
		registration->isIrq = true;

		*ret = static_cast<long>(gsi);

		if (gsi == 1000000) {
			return EFAULT;
		}

		return 0;
	}

	auto SyscallManager::syscallFreeGsi(long */*unused*/, const u64 gsi, const u64 destCpu, const u64 isLapic, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		IrqAllocator *irqAllocator = kernel->getIrqAllocator();
		const CpuManager *cpuManager = kernel->getCpuManager();

		uint64_t gsiDestCpu = destCpu;

		if (static_cast<bool>(isLapic)) {
			if (cpuManager->getBootstrapCpu()->lapicId == destCpu) {
				gsiDestCpu = cpuManager->getBootstrapCpu()->cpuId;
			} else {
				for (u64 i = 0; i < cpuManager->getCoreAmount(); i++) {
					if (cpuManager->getCoreList()[i].cpuCore.lapicId == destCpu) {
						gsiDestCpu = cpuManager->getCoreList()[i].cpuCore.cpuId;

						break;
					}
				}
			}
		}

		if (not irqAllocator->freeIrq(gsi, gsiDestCpu)) {
			return EFAULT;
		}

		auto it = irqRegistrations.begin();
		const auto end = irqRegistrations.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.irq == gsi and currEntry.isIrq) {
				irqRegistrations.remove(&currEntry);

				break;
			}

			it = nextIt;
		}

		return 0;
	}


}

namespace kernel::x86_64::hal {
	using namespace common;

	void intSyscallEntry(Frame *frame) {
		//CommonMain::getTerminal()->debug("Syscall: %lu", "Syscalls", frame->rax);

		long ret = 0;

		// TODO: Check the compat OS
		if (frame->rax >= horizonSyscallAmount || SyscallManager::horizonSyscalls[frame->rax] == nullptr) {
			frame->rax = -ENOSYS;
			return;
		}

		frame->rdx = SyscallManager::horizonSyscalls[frame->rax](&ret, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);

		frame->rax = ret;

		deliverPendingSignal(frame);
	}

	void callSyscall(SyscallRegs *regs) {
		/*if (regs->rax == 38 or regs->rax == 39) {
			CommonMain::getTerminal()->debug("Syscall: %lu", "Syscalls", regs->rax);
		}*/

		Thread *thread = Scheduler::getCurrentThread();

		if (regs->rax == signalSigreturnSyscall) {
			if (thread != nullptr && thread->hasSignalFrame()) {
				restoreRegs(thread->getSignalFrame(), regs);
				thread->clearSignalState();
			} else {
				regs->rax = EINVAL;
			}

			return;
		}

		long ret = 0;

		// TODO: Check the compat OS
		if (regs->rax >= horizonSyscallAmount || SyscallManager::horizonSyscalls[regs->rax] == nullptr) {
			regs->rax = -ENOSYS;
			return;
		}

		regs->rdx = SyscallManager::horizonSyscalls[regs->rax](&ret, regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9);

		regs->rax = ret;

		deliverPendingSignal(regs);
	}

	void deliverPendingSignal(Frame *frame) {
		deliverSignal(Scheduler::getCurrentThread(), frame);
	}

	void deliverPendingSignal(SyscallRegs *regs) {
		deliverSignal(Scheduler::getCurrentThread(), regs);
	}
}
