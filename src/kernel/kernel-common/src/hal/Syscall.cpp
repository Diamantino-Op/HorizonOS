#include "Syscall.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "Math.hpp"
#include "Futex.hpp"
#include "hal/Cpu.hpp"
#include "threading/PortMessaging.hpp"

extern volatile limine_rsdp_request rsdpRequest;
extern volatile limine_memmap_request memMapRequest;
extern volatile limine_mp_request mpRequest;

namespace kernel::common::hal {
	using namespace threading;

	namespace {
		auto isMappedAddress(const AllocContext *ctx, const u64 addr) -> bool {
			return ctx != nullptr and ctx->pageMap.getPhysAddress(addr) != 0;
		}

		auto findThreadById(Scheduler *scheduler, const u64 pid, const u64 tid) -> Thread * {
			if (scheduler == nullptr or pid == 0 or tid == 0) {
				return nullptr;
			}

			for (auto &process : scheduler->processList) {
				if (process.getId() != pid) {
					continue;
				}

				for (auto &thread : process.threadList) {
					if (thread.getId() == tid) {
						return &thread;
					}
				}

				break;
			}

			return nullptr;
		}

		auto isValidUserRange(const AllocContext *ctx, const u64 pointer, const usize size) -> bool {
			if (ctx == nullptr or pointer == 0 or size == 0) {
				return false;
			}

			const u64 end = pointer + size - 1;

			if (end < pointer) {
				return false;
			}

			return isMappedAddress(ctx, pointer) && isMappedAddress(ctx, end);
		}

		auto isValidFutexPointer(const AllocContext *ctx, const u64 pointer) -> bool {
			return isValidUserRange(ctx, pointer, sizeof(u32));
		}

		auto timespecToNs(const Timespec *ts, u64 *ns) -> int {
			if (ts == nullptr || ns == nullptr) {
				return EFAULT;
			}

			if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
				return EINVAL;
			}

			const u64 sec = static_cast<u64>(ts->tv_sec);
			const u64 nsec = static_cast<u64>(ts->tv_nsec);
			const u64 maxValue = ~0ULL;

			if (sec > (maxValue - nsec) / 1000000000ULL) {
				return EINVAL;
			}

			*ns = sec * 1000000000ULL + nsec;
			return 0;
		}

		auto getCurrentClockNs(u64 *ns) -> int {
			auto *commonMain = CommonMain::getInstance();
			const auto *clocks = commonMain != nullptr ? commonMain->getClocks() : nullptr;
			const Clock *mainClock = clocks != nullptr ? clocks->getMainClock() : nullptr;

			if (mainClock == nullptr || mainClock->getNs == nullptr) {
				return EFAULT;
			}

			*ns = mainClock->getNs();

			return 0;
		}

		auto clockIdSupported(const u64 clockId) -> bool {
			switch (clockId) {
				case 0: // CLOCK_REALTIME
				case 1: // CLOCK_MONOTONIC
				case 4: // CLOCK_MONOTONIC_RAW
				case 5: // CLOCK_REALTIME_COARSE
				case 6: // CLOCK_MONOTONIC_COARSE
				case 7: // CLOCK_BOOTTIME
					return true;

				default:
					return false;
			}
		}

		auto countProcesses(Scheduler *scheduler) -> unsigned short {
			if (scheduler == nullptr) {
				return 0;
			}

			unsigned short count = 0;
			const bool schedPrevIF = scheduler->getSchedLock()->lock();

			for (const auto &process : scheduler->processList) {
				(void)process;
				if (count != 0xffff) {
					count++;
				}
			}

			scheduler->getSchedLock()->unlock(schedPrevIF);
			return count;
		}

		auto getUsableMemoryBytes() -> u64 {
			if (memMapRequest.response == nullptr) {
				return 0;
			}

			u64 total = 0;

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				const limine_memmap_entry *entry = memMapRequest.response->entries[i];
				if (entry != nullptr && entry->type == LIMINE_MEMMAP_USABLE) {
					total += entry->length;
				}
			}

			return total;
		}

		auto copyToUser(const AllocContext *ctx, const u64 userPtr, const void *sourcePtr, const usize size) -> int {
			if (ctx == nullptr or sourcePtr == nullptr or userPtr == 0 or size == 0) {
				return EFAULT;
			}

			if (!isValidUserRange(ctx, userPtr, size)) {
				return EFAULT;
			}

			const auto *sourceBytes = reinterpret_cast<const u8 *>(sourcePtr);
			auto *hhdmBytes = reinterpret_cast<u8 *>(CommonMain::getCurrentHhdm());

			for (usize offset = 0; offset < size; offset++) {
				const u64 physAddr = ctx->pageMap.getPhysAddress(userPtr + offset);
				if (physAddr == 0) {
					return EFAULT;
				}

				hhdmBytes[physAddr] = sourceBytes[offset];
			}

			return 0;
		}
	}

	LinkedList<IrqRegistration> SyscallManager::irqRegistrations {};

	SyscallFun SyscallManager::horizonSyscalls[horizonSyscallAmount] {};
	SyscallFun SyscallManager::linuxSyscalls[linuxSyscallAmount] {};

	void SyscallManager::init() {
		horizonSyscalls[0] = &syscallPrint;
		horizonSyscalls[1] = &syscallMMap;
		horizonSyscalls[2] = &syscallMUnmap;
		horizonSyscalls[3] = &syscallGetTID;
		horizonSyscalls[4] = &syscallArchCtl;
		horizonSyscalls[5] = &syscallExit;
		horizonSyscalls[6] = &syscallClockGet;
		horizonSyscalls[7] = &syscallSysInfo;
		horizonSyscalls[8] = &syscallGetCpu;
		horizonSyscalls[9] = &syscallKillThread;
		horizonSyscalls[10] = &syscallPause;
		horizonSyscalls[11] = &syscallThreadExit;
		horizonSyscalls[12] = &syscallNewThread;
		horizonSyscalls[13] = &syscallSendMsg;
		horizonSyscalls[14] = &syscallRecvMsg;
		horizonSyscalls[15] = &syscallRegisterPort;
		horizonSyscalls[16] = &syscallIsThreadAlive;
		horizonSyscalls[17] = &syscallFutex;
		horizonSyscalls[18] = &syscallSigaction;
		horizonSyscalls[19] = &syscallSigreturn;
		horizonSyscalls[20] = &syscallMProtect;
		horizonSyscalls[21] = &syscallNanoSleep;
		horizonSyscalls[22] = &syscallIsaTTY;
		horizonSyscalls[23] = &syscallIoPerm;
		horizonSyscalls[24] = &syscallIoPl;
		horizonSyscalls[25] = &syscallKill;
		horizonSyscalls[26] = &syscallGetPID;
		horizonSyscalls[27] = &syscallMMapPhys;
		horizonSyscalls[28] = &syscallGetRsdp;
		horizonSyscalls[29] = &syscallInstallIRQHandler;
		horizonSyscalls[30] = &syscallUninstallIRQHandler;
		horizonSyscalls[31] = &syscallGetIRQMode;
		horizonSyscalls[32] = &syscallSetIntStatus;
		horizonSyscalls[33] = &syscallAllocIntVec;
		horizonSyscalls[34] = &syscallFreeIntVec;
		horizonSyscalls[35] = &syscallAllocGsi;
		horizonSyscalls[36] = &syscallFreeGsi;
		horizonSyscalls[37] = &syscallLockToCore;
		horizonSyscalls[38] = &syscallGetCpuCount;
		horizonSyscalls[39] = &syscallGetCpuIDs;

		initArch();
	}

	u32 SyscallManager::portWatchdog(u64 *) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		const bool prevIF = schedulerPtr->getSchedLock()->lock();

		auto it  = schedulerPtr->blockedThreadList.begin();
		auto end = schedulerPtr->blockedThreadList.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			// Only wake threads blocked waiting on a port message.
			if (currEntry.getWaitingPort() != 0) {
				currEntry.setWaitingPort(0);
				currEntry.setState(ThreadState::RUNNING);

				CommonMain::getTerminal()->debug("Unblocking thread: %lu", "Watchdog", currEntry.getId());

				schedulerPtr->blockedThreadList.remove(&currEntry, false);
				schedulerPtr->queues[currEntry.getParent()->getPriority()].addEnd(&currEntry);
			}

			it = nextIt;
		}

		schedulerPtr->getSchedLock()->unlock(prevIF);

		return 0;
	}

	u64 SyscallManager::syscallPrint(long *, const u64 message, u64, u64, u64, u64, u64) {
		const u16 tid = Scheduler::getCurrentThread()->getId();

		CommonMain::getTerminal()->info("Thread %u: %s", "User", tid, reinterpret_cast<char *>(message));
		CommonMain::getTerminal()->debug("Thread %u: %s", "User", tid, reinterpret_cast<char *>(message));
		//CommonMain::getTerminal()->printfBoth(true, "");

		return 0;
	}

	u64 SyscallManager::syscallMMap(long *ret, const u64 hint, const u64 size, const u64 prot, const u64 flags, u64 fd, const u64 offset) {
		// TODO: implement fd and offset

		if (size == 0) {
			*ret = MAP_FAILED;

			return EINVAL;
		}

		AllocContext *ctx = Scheduler::getCurrentThread()->getParent()->getProcessContext();

		u64 addr = hint;

		if (addr == 0) {
			addr = Scheduler::getCurrentThread()->getParent()->topmostMappedPage + pageSize;
		} else if (ctx->pageMap.getPhysAddress(addr) == 0) {
			*ret = MAP_FAILED;

			return EEXIST;
		}

		if (flags & MAP_ANON && offset != 0) {
			*ret = MAP_FAILED;

			return EINVAL;
		}

		const u64 bottomAddr = alignDown<u64>(addr, pageSize);
		const u64 topAddr = alignUp<u64>(addr + size, pageSize);

		for (u64 i = bottomAddr; i < topAddr; i += pageSize) {
			const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (physPage == nullptr) {
				*ret = MAP_FAILED;

				return ENOMEM;
			}

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(physPage), (prot & 0b11) | 0b101, false, not(prot & PROT_EXEC));

			if (i > Scheduler::getCurrentThread()->getParent()->topmostMappedPage) {
				Scheduler::getCurrentThread()->getParent()->topmostMappedPage = i;
			}
		}

		*ret = static_cast<long>(addr);

		return 0;
	}

	u64 SyscallManager::syscallMUnmap(long *, const u64 addr, const u64 size, u64, u64, u64, u64) {
		if (size == 0 || addr == 0) {
			return EINVAL;
		}

		AllocContext *ctx = Scheduler::getCurrentThread()->getParent()->getProcessContext();

		const u64 bottomAddr = alignDown<u64>(addr, pageSize);
		const u64 topAddr = alignUp<u64>(addr + size, pageSize);

		for (u64 i = bottomAddr; i < topAddr; i += pageSize) {
			ctx->pageMap.unMapPage(i);
		}

		return 0;
	}

	u64 SyscallManager::syscallGetTID(long *ret, u64, u64, u64, u64, u64, u64) {
		*ret = Scheduler::getCurrentThread()->getId();

		return 0;
	}

	u64 SyscallManager::syscallArchCtl(long *ret, const u64 operation, const u64 pointer, u64, u64, u64, u64) {
		//CommonMain::getTerminal()->debug("Arch specific syscall called with params: %lu 0x%.16lx", "Syscall Manager", operation, pointer);

		switch (operation) {
			case ARCH_CTL_SET_GSBASE:
				setGsBase(pointer);
				break;

			case ARCH_CTL_SET_FSBASE:
				setFsBase(pointer);
				break;

			case ARCH_CTL_GET_GSBASE:
				*ret = getGsBase();
				break;

			case ARCH_CTL_GET_FSBASE:
				*ret = getFsBase();
				break;

			default:
				return EFAULT;
		}

		return 0;
	}

	u64 SyscallManager::syscallExit(long *, u64 status, u64, u64, u64, u64, u64) {
		CommonMain::getTerminal()->debug("Process %lu exited with status code: %lu", "Syscall", Scheduler::getCurrentThread()->getParent()->getId(), status);
		CommonMain::getTerminal()->info("Process %lu exited with status code: %lu", "Syscall", Scheduler::getCurrentThread()->getParent()->getId(), status);

		CommonMain::getInstance()->getScheduler()->killProcess(Scheduler::getCurrentThread()->getParent());

		return 0;
	}

	u64 SyscallManager::syscallClockGet(long *ret, const u64 clock, const u64 secs, const u64 nanos, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		if (!clockIdSupported(clock)) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		u64 clockNs = 0;
		const int err = getCurrentClockNs(&clockNs);

		if (err != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return err;
		}

		*reinterpret_cast<long *>(secs) = static_cast<long>(clockNs / nanosecondsPerSecond);
		*reinterpret_cast<long *>(nanos) = static_cast<long>(clockNs % nanosecondsPerSecond);

		if (ret != nullptr) {
			*ret = 0;
		}

		return 0;
	}

	u64 SyscallManager::syscallSysInfo(long *ret, const u64 info, u64, u64, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		auto *commonMain = CommonMain::getInstance();
		auto *scheduler = commonMain != nullptr ? commonMain->getScheduler() : nullptr;
		const Thread *thread = Scheduler::getCurrentThread();
		const auto *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (!isValidUserRange(ctx, info, sizeof(KernelSysInfo))) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		u64 uptimeNs = 0;
		const int clockErr = getCurrentClockNs(&uptimeNs);
		if (clockErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return clockErr;
		}

		// TODO
		KernelSysInfo kernelInfo {};
		kernelInfo.uptime = static_cast<long>(uptimeNs / nanosecondsPerSecond);
		kernelInfo.loads[0] = 0;
		kernelInfo.loads[1] = 0;
		kernelInfo.loads[2] = 0;
		kernelInfo.totalram = getUsableMemoryBytes();
		kernelInfo.freeram = commonMain != nullptr && commonMain->getPMM() != nullptr ? commonMain->getPMM()->getFreeMemory() : 0;
		kernelInfo.sharedram = 0;
		kernelInfo.bufferram = 0;
		kernelInfo.totalswap = 0;
		kernelInfo.freeswap = 0;
		kernelInfo.procs = countProcesses(scheduler);
		kernelInfo.totalhigh = 0;
		kernelInfo.freehigh = 0;
		kernelInfo.mem_unit = 1;

		const int copyErr = copyToUser(ctx, info, &kernelInfo, sizeof(kernelInfo));
		if (copyErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return copyErr;
		}

		if (ret != nullptr) {
			*ret = 0;
		}

		return 0;
	}

	u64 SyscallManager::syscallKillThread(long *ret, u64 pid, u64 tid, u64 sig, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		if (sig == 0 || sig > signalActionCount || pid == 0 || tid == 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		if (scheduler == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		Thread *target = findThreadById(scheduler, pid, tid);

		if (target == nullptr) {
			scheduler->getSchedLock()->unlock(schedPrevIF);

			if (ret != nullptr) {
				*ret = -1;
			}

			return ESRCH;
		}

		target->queueSignal(sig);
		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	u64 SyscallManager::syscallPause(long *ret, u64, u64, u64, u64, u64, u64) {
		return 0;
	}

	u64 SyscallManager::syscallThreadExit(long *, u64, u64, u64, u64, u64, u64) {
		return 0;
	}

	u64 SyscallManager::syscallNewThread(long *ret, const u64 entryFun, const u64 stack, u64, u64, u64, u64) {
		// TODO: Check

		CommonMain::getTerminal()->debug("New Thread: Stack: 0x%.16lx", "Syscall Manager", stack);

		auto *scheduler = CommonMain::getInstance()->getScheduler();
		const auto *currentThread = Scheduler::getCurrentThread();
		auto *process = currentThread != nullptr ? currentThread->getParent() : nullptr;

		if (scheduler == nullptr || process == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return ESRCH;
		}

		auto *newThread = new Thread(scheduler, process, entryFun, true, 0, stack);
		newThread->setState(ThreadState::READY);

		const bool prevIF = scheduler->getSchedLock()->lock();
		process->addThread(newThread);
		scheduler->readyThreadList.addStart(newThread);
		scheduler->getSchedLock()->unlock(prevIF);

		if (ret != nullptr) {
			*ret = static_cast<long>(newThread->getId());
		}

		return 0;
	}

	u64 SyscallManager::syscallSendMsg(long *ret, const u64 sendPort, const u64 port, const u64 msgHdr, u64, u64, u64) {
		auto *hdr = reinterpret_cast<MessageHeader *>(msgHdr);
		auto *scheduler = CommonMain::getInstance()->getScheduler();
		auto *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr or thread == nullptr or thread->getParent() == nullptr or hdr == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (!isValidUserRange(thread->getParent()->getProcessContext(), msgHdr, sizeof(MessageHeader))) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (hdr->length > 0 && !isValidUserRange(thread->getParent()->getProcessContext(), reinterpret_cast<u64>(hdr->buffer), hdr->length)) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		for (u64 retryAttempt = 0;; ++retryAttempt) {
			const u64 result = PortMessaging::sendMessage(sendPort, port, hdr);

			if (result == 0) {
				if (ret != nullptr) {
					*ret = hdr->retLength;
				}

				return 0;
			}

			if (result != ENOENT) {
				if (ret != nullptr) {
					*ret = -1;
				}

				return result;
			}

			if (retryAttempt >= sendMessageRetryCount) {
				if (ret != nullptr) {
					*ret = -1;
				}

				return result;
			}

			scheduler->sleepThread(thread, sendMessageRetrySleepMs * 1000000ULL);
		}
	}

	u64 SyscallManager::syscallRecvMsg(long *ret, const u64 port, const u64 msgHdr, const u64 options, u64, u64, u64) {
		auto *hdr = reinterpret_cast<MessageHeader *>(msgHdr);
		auto *scheduler = CommonMain::getInstance()->getScheduler();
		auto *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr or thread == nullptr or thread->getParent() == nullptr or hdr == nullptr) {
			return EFAULT;
		}

		if (!isValidUserRange(thread->getParent()->getProcessContext(), msgHdr, sizeof(MessageHeader))) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (options != 0 && !isValidUserRange(thread->getParent()->getProcessContext(), options, sizeof(MessageFilterOptions))) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (hdr->length > 0 && !isValidUserRange(thread->getParent()->getProcessContext(), reinterpret_cast<u64>(hdr->buffer), hdr->length)) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		const u64 result = PortMessaging::recvMessage(port, hdr, reinterpret_cast<MessageFilterOptions *>(options));

		if (result != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return result;
		}

		if (ret != nullptr) {
			*ret = hdr->retLength;
		}

		return 0;
	}

	u64 SyscallManager::syscallRegisterPort(long *ret, const u64 preferredPort, u64, u64, u64, u64, u64) {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (preferredPort == 0) {
			*ret = static_cast<long>(PortMessaging::getNewPort());
		} else {
			*ret = preferredPort;
		}

		return PortMessaging::registerPort(*ret);
	}

	u64 SyscallManager::syscallFutex(long *ret, const u64 pointer, const u64 type, const u64 expected, const u64 time, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (thread == nullptr or thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		const AllocContext *ctx = thread->getParent()->getProcessContext();

		const u64 futexOp = type & FUTEX_CMD_MASK;

		switch (futexOp) {
			case FUTEX_WAIT: {
				if (time != 0) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EINVAL;
				}

				if (!isValidFutexPointer(ctx, pointer)) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EFAULT;
				}

				const auto *futexWord = reinterpret_cast<volatile u32 *>(pointer);

				if (static_cast<u32>(*futexWord) != static_cast<u32>(expected)) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EAGAIN;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				if (!isValidFutexPointer(ctx, pointer)) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return EFAULT;
				}

				if (static_cast<u32>(*reinterpret_cast<volatile u32 *>(pointer)) != static_cast<u32>(expected)) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return EAGAIN;
				}

				if (!Futex::addWaiter(pointer, thread->getId())) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return ENOMEM;
				}

				thread->setState(ThreadState::BLOCKED);
				scheduler->removeThread(thread);

				const LinkedListEntry<Thread> *currentEntry = Scheduler::getCurrentExecutionNode()->getCurrentThread();
				const bool shouldReschedule = currentEntry != nullptr && currentEntry->value == thread;

				scheduler->blockedThreadList.addStart(thread);

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (shouldReschedule) {
					ExecutionNode::reSchedule();
				}

				if (ret != nullptr) {
					*ret = 0;
				}

				return 0;
			}

			case FUTEX_WAIT_BITSET: {
				if (!isValidFutexPointer(ctx, pointer)) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EFAULT;
				}

				const auto *futexWord = reinterpret_cast<volatile u32 *>(pointer);

				if (static_cast<u32>(*futexWord) != static_cast<u32>(expected)) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EAGAIN;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				if (!isValidFutexPointer(ctx, pointer)) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return EFAULT;
				}

				if (static_cast<u32>(*reinterpret_cast<volatile u32 *>(pointer)) != static_cast<u32>(expected)) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return EAGAIN;
				}

				if (!Futex::addWaiter(pointer, thread->getId())) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					if (ret != nullptr) {
						*ret = -1;
					}

					return ENOMEM;
				}

				thread->setState(ThreadState::BLOCKED);
				scheduler->removeThread(thread);

				const LinkedListEntry<Thread> *currentEntry = Scheduler::getCurrentExecutionNode()->getCurrentThread();
				const bool shouldReschedule = currentEntry != nullptr && currentEntry->value == thread;

				scheduler->blockedThreadList.addStart(thread);

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (shouldReschedule) {
					ExecutionNode::reSchedule();
				}

				if (ret != nullptr) {
					*ret = 0;
				}

				return 0;
			}

			case FUTEX_WAKE: {
				if (time != 0) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EINVAL;
				}

				if (expected == 0) {
					if (ret != nullptr) {
						*ret = 0;
					}

					return 0;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				u64 woken = 0;
				u16 threadId = 0;

				while (woken < expected && Futex::popWaiter(pointer, &threadId)) {
					Thread *waitThread = scheduler->getThread(threadId);

					if (waitThread != nullptr && waitThread->getState() == ThreadState::BLOCKED) {
						scheduler->blockedThreadList.remove(waitThread, false);
						waitThread->setState(ThreadState::RUNNING);
						waitThread->setSleepNs(0);
						scheduler->queues[waitThread->getParent()->getPriority()].addEnd(waitThread);
						woken++;
					}
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (ret != nullptr) {
					*ret = static_cast<long>(woken);
				}

				return 0;
			}

			case FUTEX_WAKE_BITSET: {
				if (time != 0) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return EINVAL;
				}

				if (expected == 0) {
					if (ret != nullptr) {
						*ret = 0;
					}

					return 0;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				u64 woken = 0;
				u16 threadId = 0;

				while (woken < expected && Futex::popWaiter(pointer, &threadId)) {
					Thread *waitThread = scheduler->getThread(threadId);

					if (waitThread != nullptr && waitThread->getState() == ThreadState::BLOCKED) {
						scheduler->blockedThreadList.remove(waitThread, false);
						waitThread->setState(ThreadState::RUNNING);
						waitThread->setSleepNs(0);
						scheduler->queues[waitThread->getParent()->getPriority()].addEnd(waitThread);
						woken++;
					}
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (ret != nullptr) {
					*ret = static_cast<long>(woken);
				}

				return 0;
			}

			default:
				if (ret != nullptr) {
					*ret = -1;
				}

				return EINVAL;
		}
	}

	u64 SyscallManager::syscallSigaction(long *ret, const u64 sig, const u64 action, const u64 oldAction, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (sig == 0 || sig > signalActionCount) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		constexpr u64 sigKill = 9;
		constexpr u64 sigStop = 19;

		if (action != 0 && (sig == sigKill || sig == sigStop)) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		const AllocContext *ctx = thread->getParent()->getProcessContext();

		if (!isValidUserRange(ctx, action, action != 0 ? sizeof(SignalAction) : 0)) {
			if (action != 0) {
				if (ret != nullptr) {
					*ret = -1;
				}

				return EFAULT;
			}
		}

		if (!isValidUserRange(ctx, oldAction, oldAction != 0 ? sizeof(SignalAction) : 0)) {
			if (oldAction != 0) {
				if (ret != nullptr) {
					*ret = -1;
				}

				return EFAULT;
			}
		}

		SignalAction newActionCopy {};

		if (action != 0) {
			newActionCopy = *reinterpret_cast<const SignalAction *>(action);
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		const auto currentAction = thread->getParent()->signalActions[sig - 1];

		if (oldAction != 0) {
			*reinterpret_cast<SignalAction *>(oldAction) = currentAction;
		}

		if (action != 0) {
			thread->getParent()->signalActions[sig - 1] = newActionCopy;
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		if (ret != nullptr) {
			*ret = 0;
		}

		return 0;
	}

	u64 SyscallManager::syscallSigreturn(long *ret, u64, u64, u64, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (!thread->hasSignalFrame()) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		thread->clearSignalState();

		return 0;
	}

	u64 SyscallManager::syscallMProtect(long *ret, const u64 pointer, const u64 size, const u64 prot, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		CommonMain::getTerminal()->debug("Memprotect: Prot: %lu, Size: %lu, Addr: 0x%.16lx", "Syscall Manager", prot, size, pointer);

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		if (pointer == 0 || size == 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		constexpr u64 supportedProt = PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC;
		if ((prot & ~supportedProt) != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		const u64 end = pointer + size - 1;
		if (end < pointer) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EINVAL;
		}

		AllocContext *ctx = thread->getParent()->getProcessContext();
		const u64 bottomAddr = alignDown<u64>(pointer, pageSize);
		const u64 topAddr = alignUp<u64>(pointer + size, pageSize);
		const bool schedPrevIF = scheduler->getSchedLock()->lock();

		for (u64 addr = bottomAddr; addr < topAddr; addr += pageSize) {
			if (ctx->pageMap.getPhysAddress(addr) == 0) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (ret != nullptr) {
					*ret = -1;
				}

				return EFAULT;
			}
		}

		for (u64 addr = bottomAddr; addr < topAddr; addr += pageSize) {
			if (!ctx->pageMap.protectPage(addr, static_cast<u8>(prot))) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (ret != nullptr) {
					*ret = -1;
				}

				return EFAULT;
			}
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		if (ret != nullptr) {
			*ret = 0;
		}

		return 0;
	}

	u64 SyscallManager::syscallNanoSleep(long *ret, const u64 secs, const u64 nanos, u64, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		const Timespec ts = {
			.tv_sec = static_cast<long>(secs),
			.tv_nsec = static_cast<long>(nanos)
		};

		u64 sleepNs = 0;
		const int err = timespecToNs(&ts, &sleepNs);
		if (err != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return err;
		}

		if (sleepNs == 0) {
			if (ret != nullptr) {
				*ret = 0;
			}

			return 0;
		}

		scheduler->sleepThread(thread, sleepNs);

		if (ret != nullptr) {
			*ret = 0;
		}

		return 0;
	}

	u64 SyscallManager::syscallIsaTTY(long *ret, u64 fd, u64, u64, u64, u64, u64) {
		// TODO: Unstub
		return 0;
	}

	u64 SyscallManager::syscallKill(long *ret, const u64 pid, u64 signal, u64, u64, u64, u64) {
		Scheduler *sched = CommonMain::getInstance()->getScheduler();

		sched->killProcess(sched->getProcess(pid));

		return 0;
	}

	u64 SyscallManager::syscallGetPID(long *ret, u64, u64, u64, u64, u64, u64) {
		*ret = Scheduler::getCurrentThread()->getParent()->getId();

		return 0;
	}

	u64 SyscallManager::syscallMMapPhys(long *ret, const u64 physAddr, const u64 len, u64, u64, u64, u64) {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (physAddr == 0 || len == 0) {
			return EINVAL;
		}

		const auto *thread = Scheduler::getCurrentThread();
		const auto *process = thread != nullptr ? thread->getParent() : nullptr;
		auto *kernelCtx = CommonMain::getInstance()->getKernelAllocContext();
		auto *processCtx = process != nullptr ? process->getProcessContext() : nullptr;

		if (thread == nullptr || process == nullptr || kernelCtx == nullptr || processCtx == nullptr) {
			return EFAULT;
		}

		const u64 alignedAddr = alignDown<u64>(physAddr, pageSize);

		if (len > ~0ULL - physAddr) {
			return EINVAL;
		}

		const u64 endAddr = physAddr + len;
		const u64 roundedLen = alignUp<u64>(endAddr, pageSize);
		const u64 hhdmBase = CommonMain::getCurrentHhdm();

		for (u64 i = alignedAddr; i < roundedLen; i += pageSize) {
			const u64 virtAddr = i + hhdmBase;

			kernelCtx->pageMap.mapPage(virtAddr, i, 0b00000111, false, false);
			processCtx->pageMap.mapPage(virtAddr, i, 0b00000111, false, false);

			PageMap::invPg(virtAddr);
		}

		*ret = static_cast<long>(physAddr + hhdmBase);

		return 0;
	}

	u64 SyscallManager::syscallGetRsdp(long *ret, u64, u64, u64, u64, u64, u64) {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (rsdpRequest.response == nullptr) {
			*ret = 0;

			return EFAULT;
		}

		*ret = static_cast<long>(reinterpret_cast<uacpi_phys_addr>(rsdpRequest.response->address) - CommonMain::getCurrentHhdm());

		return 0;
	}

	u64 SyscallManager::syscallLockToCore(long *, u64 cpuId, u64, u64, u64, u64, u64) {
		Scheduler::getCurrentThread()->setLockedCoreId(cpuId);

		return 0;
	}

	u64 SyscallManager::syscallGetCpuCount(long *ret, u64, u64, u64, u64, u64, u64) {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (mpRequest.response == nullptr) {
			*ret = 0;

			return EFAULT;
		}

		*ret = static_cast<long>(mpRequest.response->cpu_count);

		return 0;
	}

	u64 SyscallManager::syscallGetCpuIDs(long *ret, u64 cpuCount, u64, u64, u64, u64, u64) {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (mpRequest.response == nullptr) {
			return EFAULT;
		}

		if (cpuCount > mpRequest.response->cpu_count) {
			return EINVAL;
		}

		for (u64 i = 0; i < cpuCount; i++) {
			ret[i] = mpRequest.response->cpus[i]->processor_id;
		}

		return 0;
	}
}
