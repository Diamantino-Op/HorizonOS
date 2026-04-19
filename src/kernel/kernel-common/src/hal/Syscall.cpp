#include "Syscall.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "Math.hpp"
#include "Futex.hpp"
#include "threading/PortMessaging.hpp"

namespace kernel::common::hal {
	using namespace threading;

	namespace {
		auto isMappedAddress(const AllocContext *ctx, const u64 addr) -> bool {
			return ctx != nullptr && ctx->pageMap.getPhysAddress(addr) != 0;
		}

		auto isValidFutexPointer(const AllocContext *ctx, const u64 pointer) -> bool {
			return pointer != 0 && isMappedAddress(ctx, pointer) && isMappedAddress(ctx, pointer + sizeof(u32) - 1);
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

		initArch();
	}

	u64 SyscallManager::syscallPrint(long *, const u64 message, u64, u64, u64, u64, u64) {
		CommonMain::getTerminal()->info(reinterpret_cast<char *>(message), "User");
		CommonMain::getTerminal()->printf(true, "");

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

		auto *scheduler = CommonMain::getInstance()->getScheduler();
		const auto *currentThread = Scheduler::getCurrentThread();
		auto *process = currentThread != nullptr ? currentThread->getParent() : nullptr;

		if (scheduler == nullptr || process == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return ESRCH;
		}

		auto *newThread = new Thread(scheduler, process, entryFun, true, stack);
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

		switch (type) {
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

				if (!shouldReschedule) {
					scheduler->blockedThreadList.addStart(thread);
				}

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

			default:
				if (ret != nullptr) {
					*ret = -1;
				}

				return EINVAL;
		}
	}
}