#include "Syscall.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "Math.hpp"
#include "Futex.hpp"
#include "threading/PortMessaging.hpp"

namespace kernel::common::hal {
	using namespace threading;

	namespace {
		struct SleepTimespec {
			long tv_sec;
			long tv_nsec;
		};

		auto isMappedAddress(const AllocContext *ctx, const u64 addr) -> bool {
			return ctx != nullptr && ctx->pageMap.getPhysAddress(addr) != 0;
		}

		auto findThreadById(Scheduler *scheduler, const u64 pid, const u64 tid) -> Thread * {
			if (scheduler == nullptr || pid == 0 || tid == 0) {
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
			if (ctx == nullptr || pointer == 0 || size == 0) {
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

		auto timespecToNs(const SleepTimespec *ts, u64 *ns) -> int {
			if (ts == nullptr || ns == nullptr) {
				return EFAULT;
			}

			CommonMain::getTerminal()->debug("AA", "User");

			if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
				return EINVAL;
			}

			CommonMain::getTerminal()->debug("AB", "User");

			const u64 sec = static_cast<u64>(ts->tv_sec);
			const u64 nsec = static_cast<u64>(ts->tv_nsec);
			const u64 maxValue = ~0ULL;

			if (sec > (maxValue - nsec) / 1000000000ULL) {
				return EINVAL;
			}

			CommonMain::getTerminal()->debug("AC", "User");

			*ns = sec * 1000000000ULL + nsec;
			return 0;
		}
	}

	SyscallFun SyscallManager::horizonSyscalls[horizonSyscallAmount]{};
	SyscallFun SyscallManager::linuxSyscalls[linuxSyscallAmount]{};

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

		initArch();
	}

	u64 SyscallManager::syscallPrint(long *, const u64 message, u64, u64, u64, u64, u64) {
		CommonMain::getTerminal()->info(reinterpret_cast<char *>(message), "User");
		CommonMain::getTerminal()->debug(reinterpret_cast<char *>(message), "User");
		CommonMain::getTerminal()->printfBoth(true, "");

		return 0;
	}

	u64 SyscallManager::syscallMMap(long *ret, u64 hint, u64 size, u64 prot, u64 flags, u64 fd, u64 offset) {
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

	u64 SyscallManager::syscallMUnmap(long *, u64 addr, u64 size, u64, u64, u64, u64) {
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

	// Get TID is arch specific

	u64 SyscallManager::syscallArchCtl(long *ret, const u64 operation, const u64 pointer, u64, u64, u64, u64) {
		CommonMain::getTerminal()->debug("Arch specific syscall called with params: %lu 0x%.16lx", "Syscall Manager", operation, pointer);

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

	u64 SyscallManager::syscallExit(long *, u64 staus, u64, u64, u64, u64, u64) {
		CommonMain::getInstance()->getScheduler()->killProcess(Scheduler::getCurrentThread()->getParent());

		return 0;
	}

	u64 SyscallManager::syscallClockGet(long *ret, u64 clock, u64 ts, u64, u64, u64, u64) {
		return 0;
	}

	u64 SyscallManager::syscallSysInfo(long *ret, u64 info, u64, u64, u64, u64, u64) {
		return 0;
	}

	u64 SyscallManager::syscallGetCpu(long *ret, u64, u64, u64, u64, u64, u64) {
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

	u64 SyscallManager::syscallSendMsg(long *ret, const u64 port, const u64 msgHdr, u64, u64, u64, u64) {
		auto *hdr = reinterpret_cast<MessageHeader *>(msgHdr);

		const u64 result = PortMessaging::sendMessage(port, hdr);

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

	u64 SyscallManager::syscallRecvMsg(long *ret, const u64 port, const u64 msgHdr, u64, u64, u64, u64) {
		auto *hdr = reinterpret_cast<MessageHeader *>(msgHdr);

		const u64 result = PortMessaging::recvMessage(port, hdr);

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

	u64 SyscallManager::syscallRegisterPort(long *ret, const u64 port, u64, u64, u64, u64, u64) {
		const u64 result = PortMessaging::registerPort(port);

		if (result != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return result;
		}

		if (ret != nullptr) {
			*ret = static_cast<long>(port);
		}

		return 0;
	}

	u64 SyscallManager::syscallFutex(long *ret, u64 pointer, u64 type, u64 expected, u64 time, u64, u64) {
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

	u64 SyscallManager::syscallSigaction(long *ret, u64 sig, u64 action, u64 oldAction, u64, u64, u64) {
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

	u64 SyscallManager::syscallMProtect(long *ret, u64 pointer, u64 size, u64 prot, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		CommonMain::getTerminal()->debug("Memprotect: Prot: %lu, Size: %lu, Addr: 0x%.16lx", "Syscall Manager", prot, size, pointer);

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

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

	u64 SyscallManager::syscallNanoSleep(long *ret, u64 ts, u64, u64, u64, u64, u64) {
		if (ret != nullptr) {
			*ret = 0;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		CommonMain::getTerminal()->debug("A", "User");

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		CommonMain::getTerminal()->debug("B", "User");

		const AllocContext *ctx = thread->getParent()->getProcessContext();
		if (!isValidUserRange(ctx, ts, sizeof(SleepTimespec))) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		CommonMain::getTerminal()->debug("C", "User");

		u64 sleepNs = 0;
		const int err = timespecToNs(reinterpret_cast<const SleepTimespec *>(ts), &sleepNs);
		if (err != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return err;
		}

		CommonMain::getTerminal()->debug("D", "User");

		if (sleepNs == 0) {
			if (ret != nullptr) {
				*ret = 0;
			}

			return 0;
		}

		CommonMain::getTerminal()->debug("E", "User");

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
}
