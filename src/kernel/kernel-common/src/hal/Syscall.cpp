#include "Syscall.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "Math.hpp"
#include "Futex.hpp"
#include "hal/Cpu.hpp"
#include "memory/VirtualAllocator.hpp"
#include "threading/PortMessaging.hpp"
#include "limine.h"

extern limine_mp_request mpRequest;
extern limine_framebuffer_request framebufferRequest;

namespace kernel::common::hal {
	using namespace threading;

	namespace {
		constexpr u64 unlockedAffinityCpuId = ~0x0U;
		constexpr usize affinityBitsPerByte = 8;
		constexpr usize maxPrintMessageLength = 1024;

		auto findThreadById(const Scheduler *scheduler, const u64 pid, const u64 tid) -> Thread * {
			if (scheduler == nullptr) {
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

			auto isValidUserRange(const AllocContext *ctx, const u64 pointer, const usize size, const bool write) -> bool {
				if (ctx == nullptr or pointer == 0 or size == 0) {
					return false;
				}

				const u64 end = pointer + size - 1;

				if (end < pointer) {
					return false;
				}

				const u64 userTop = memory::VirtualAllocator::getProcessAllocStart();

				if (pointer >= userTop || end >= userTop) {
					return false;
				}

				return ctx->pageMap.isUserAccessible(pointer, write) && ctx->pageMap.isUserAccessible(end, write);
			}

		auto isFullyMappedUserRange(const AllocContext *ctx, const u64 pointer, const usize size, const bool write) -> bool {
			if (!isValidUserRange(ctx, pointer, size, write)) {
				return false;
			}

			const u64 end = pointer + size - 1;
			u64 addr = alignDown<u64>(pointer, pageSize);

			while (addr <= end) {
				if (!ctx->pageMap.isUserAccessible(addr, write)) {
					return false;
				}

				if (addr > ~0ULL - pageSize) {
					break;
				}

				addr += pageSize;
			}

			return true;
		}

		auto isValidFutexPointer(const AllocContext *ctx, const u64 pointer) -> bool {
			return isValidUserRange(ctx, pointer, sizeof(u32), false);
		}

		auto timespecToNs(const Timespec *ts, u64 *ns) -> int {
			if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
				return EINVAL;
			}

			const u64 sec = static_cast<u64>(ts->tv_sec);
			const u64 nsec = static_cast<u64>(ts->tv_nsec);
			constexpr u64 maxValue = ~0ULL;

			if (sec > (maxValue - nsec) / 1000000000ULL) {
				return EINVAL;
			}

			*ns = (sec * 1000000000ULL) + nsec;
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

		auto copyToUser(const AllocContext *ctx, const u64 userPtr, const void *sourcePtr, const usize size) -> int {
			if (ctx == nullptr or userPtr == 0) {
				return EFAULT;
			}

			if (!isFullyMappedUserRange(ctx, userPtr, size, true)) {
				return EFAULT;
			}

			const auto *sourceBytes = static_cast<const u8 *>(sourcePtr);
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

		auto copyFromUser(const AllocContext *ctx, void *destPtr, const u64 userPtr, const usize size) -> int {
			if (ctx == nullptr or userPtr == 0) {
				return EFAULT;
			}

			if (!isFullyMappedUserRange(ctx, userPtr, size, false)) {
				return EFAULT;
			}

			auto *destBytes = static_cast<u8 *>(destPtr);
			const auto *hhdmBytes = reinterpret_cast<const u8 *>(CommonMain::getCurrentHhdm());

			for (usize offset = 0; offset < size; offset++) {
				const u64 physAddr = ctx->pageMap.getPhysAddress(userPtr + offset);

				if (physAddr == 0) {
					return EFAULT;
				}

				destBytes[offset] = hhdmBytes[physAddr];
			}

			return 0;
		}

		auto futexTimeoutFromUser(const AllocContext *ctx, const u64 timePtr, u64 *timeoutNs) -> int {
			*timeoutNs = 0;

			if (timePtr == 0) {
				return 0;
			}

			Timespec timeout {};
			const int copyErr = copyFromUser(ctx, &timeout, timePtr, sizeof(timeout));

			if (copyErr != 0) {
				return copyErr;
			}

			return timespecToNs(&timeout, timeoutNs);
		}

		struct CopiedMessageFilterOptions {
			MessageFilterOptions options {};
			u64 *blackListTypes {};
			u64 *whiteListTypes {};
		};

		auto releaseCopiedMessageFilterOptions(CopiedMessageFilterOptions *copied) -> void {
			if (copied == nullptr) {
				return;
			}

			delete[] copied->blackListTypes;
			delete[] copied->whiteListTypes;

			copied->options.blackListTypes = nullptr;
			copied->options.whiteListTypes = nullptr;
			copied->options.blackListCount = 0;
			copied->options.whiteListCount = 0;
			copied->blackListTypes = nullptr;
			copied->whiteListTypes = nullptr;
		}

		auto copyMessageFilterArray(const AllocContext *ctx, const u64 *userTypes, const usize count, u64 **kernelTypes) -> int {
			if (count == 0) {
				*kernelTypes = nullptr;
				return 0;
			}

			if (userTypes == nullptr || count > (~static_cast<usize>(0) / sizeof(u64))) {
				return EFAULT;
			}

			const usize bytes = count * sizeof(u64);
			auto *types = new u64[count];

			if (types == nullptr) {
				return ENOMEM;
			}

			const int err = copyFromUser(ctx, types, reinterpret_cast<u64>(userTypes), bytes);

			if (err != 0) {
				delete[] types;
				return err;
			}

			*kernelTypes = types;
			return 0;
		}

		auto copyMessageFilterOptionsFromUser(const AllocContext *ctx, const u64 userOptions, CopiedMessageFilterOptions *copied) -> int {
			if (userOptions == 0) {
				return 0;
			}

			const int headerErr = copyFromUser(ctx, &copied->options, userOptions, sizeof(copied->options));

			if (headerErr != 0) {
				return headerErr;
			}

			int err = copyMessageFilterArray(ctx, copied->options.blackListTypes, copied->options.blackListCount, &copied->blackListTypes);

			if (err != 0) {
				releaseCopiedMessageFilterOptions(copied);
				return err;
			}

			err = copyMessageFilterArray(ctx, copied->options.whiteListTypes, copied->options.whiteListCount, &copied->whiteListTypes);

			if (err != 0) {
				releaseCopiedMessageFilterOptions(copied);
				return err;
			}

			copied->options.blackListTypes = copied->blackListTypes;
			copied->options.whiteListTypes = copied->whiteListTypes;

			return 0;
		}

		auto writeUserByte(const AllocContext *ctx, const u64 userPtr, const u8 byte) -> int {
			return copyToUser(ctx, userPtr, &byte, sizeof(byte));
		}

		auto readUserByte(const AllocContext *ctx, const u64 userPtr, u8 *byte) -> int {
			return copyFromUser(ctx, byte, userPtr, sizeof(*byte));
		}

		auto setUserCpuMaskBit(const AllocContext *ctx, const u64 mask, const usize cpuSetSize, const u64 cpuId) -> int {
			const usize byteOffset = cpuId / affinityBitsPerByte;

			if (byteOffset >= cpuSetSize) {
				return EINVAL;
			}

			u8 byte = 0;

			const int err = readUserByte(ctx, mask + byteOffset, &byte);

			if (err != 0) {
				return err;
			}

			byte |= static_cast<u8>(1U << (cpuId % affinityBitsPerByte));

			return writeUserByte(ctx, mask + byteOffset, byte);
		}

		auto getUserCpuMaskBit(const AllocContext *ctx, const u64 mask, const usize cpuSetSize, const u64 cpuId, bool *isSet) -> int {
			const usize byteOffset = cpuId / affinityBitsPerByte;

			if (byteOffset >= cpuSetSize) {
				return EINVAL;
			}

			u8 byte = 0;
			const int err = readUserByte(ctx, mask + byteOffset, &byte);
			if (err != 0) {
				return err;
			}

			*isSet = (byte & static_cast<u8>(1U << (cpuId % affinityBitsPerByte))) != 0;

			return 0;
		}

		auto getOnlineCpuInfo(u64 *onlineCount, u64 *maxCpuId) -> int {
			if (mpRequest.response == nullptr) {
				return EFAULT;
			}

			*onlineCount = 0;
			*maxCpuId = 0;

			for (u64 i = 0; i < mpRequest.response->cpu_count; i++) {
				const limine_mp_info *cpuInfo = mpRequest.response->cpus[i];

				if (cpuInfo == nullptr) {
					continue;
				}

				const u64 cpuId = cpuInfo->processor_id;

				if (Scheduler::getCoreEN(cpuId) == nullptr) {
					continue;
				}

				(*onlineCount)++;

				if (cpuId > *maxCpuId) {
					*maxCpuId = cpuId;
				}
			}

			return *onlineCount == 0 ? EFAULT : 0;
		}

		auto findAffinityTarget(Scheduler *scheduler, const u64 tidPid) -> Thread * {
			if (scheduler == nullptr) {
				return nullptr;
			}

			if (tidPid == 0) {
				return Scheduler::getCurrentThread();
			}

			if (tidPid > 0xffff) {
				return nullptr;
			}

			if (Thread *thread = scheduler->getThread(static_cast<u16>(tidPid))) {
				return thread;
			}

			for (auto &process : scheduler->processList) {
				if (process.getId() != static_cast<u16>(tidPid)) {
					continue;
				}

				const LinkedListEntry<Thread> *firstThread = process.threadList.getFirst();

				return firstThread != nullptr ? firstThread->value : nullptr;
			}

			return nullptr;
		}

		auto findOwnedUserPhysPageUnlocked(const u64 ownerPid, const u64 pageAddr) -> UserPhysPage * {
			for (auto &page : SyscallManager::userPhysPages) {
				if (page.ownerPid == ownerPid && page.address == pageAddr) {
					return &page;
				}
			}

			return nullptr;
		}
	}

	LinkedList<IrqRegistration> SyscallManager::irqRegistrations {};
	LinkedList<KernelEventRegistration> SyscallManager::eventRegistrations {};
	LinkedList<UserPhysPage> SyscallManager::userPhysPages {};

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
		horizonSyscalls[37] = &syscallGetCpuIDs;
		horizonSyscalls[38] = &syscallAllocPhysPage;
		horizonSyscalls[39] = &syscallFreePhysPage;
		horizonSyscalls[40] = &syscallGetAffinity;
		horizonSyscalls[41] = &syscallSetAffinity;
		horizonSyscalls[42] = &syscallRegisterEventHandler;
		horizonSyscalls[43] = &syscallGetFramebufferInfo;
		horizonSyscalls[44] = &syscallReadKernelLog;

		initArch();
	}

	namespace {
		void notifyKernelEventHandlers(const u64 eventType, const u64 pid, const u64 tid) {
			KernelEventData data {};
			data.eventType = eventType;
			data.pid = pid;
			data.tid = tid;

			MessageHeader msg {};
			msg.port = 0;
			msg.type = kernelEventMsgType;
			msg.buffer = reinterpret_cast<u64 *>(&data);
			msg.length = sizeof(data);

			for (const auto &registration : SyscallManager::eventRegistrations) {
				if ((registration.eventMask & eventType) == 0) {
					continue;
				}

				PortMessaging::sendMessage(0, registration.port, &msg);
			}
		}
	}

	void SyscallManager::notifyThreadKilled(const u64 pid, const u64 tid) {
		notifyKernelEventHandlers(kernelEventThreadKilled, pid, tid);
	}

	void SyscallManager::notifyProcessKilled(const u64 pid) {
		notifyKernelEventHandlers(kernelEventProcessKilled, pid, 0);
	}

	auto SyscallManager::portWatchdog(u64 */*unused*/) -> u32 {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		const bool prevIF = schedulerPtr->getSchedLock()->lock();

		auto it  = schedulerPtr->blockedThreadList.begin();
		const auto end = schedulerPtr->blockedThreadList.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			// Only wake threads blocked waiting on a port message.
			if (currEntry.getWaitingPort() != 0) {
				currEntry.setWaitingPort(0);
				currEntry.setState(ThreadState::RUNNING);

				CommonMain::getTerminal()->debug("Unblocking thread: %lu", "Watchdog", currEntry.getId());

				schedulerPtr->blockedThreadList.remove(&currEntry, false, false);
				schedulerPtr->enqueueThread(&currEntry, true);
			}

			it = nextIt;
		}

		schedulerPtr->getSchedLock()->unlock(prevIF);

		return 0;
	}

	auto SyscallManager::syscallPrint(long */*unused*/, const u64 message, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		const u16 tid = Scheduler::getCurrentThread()->getId();
		char userMessage[maxPrintMessageLength] {};
		usize copiedLength = 0;

		for (; copiedLength < maxPrintMessageLength - 1; copiedLength++) {
			u8 byte = 0;
			const int err = readUserByte(Scheduler::getCurrentThread()->getParent()->getProcessContext(), message + copiedLength, &byte);

			if (err != 0) {
				return err;
			}

			userMessage[copiedLength] = static_cast<char>(byte);

			if (byte == '\0') {
				break;
			}
		}

		userMessage[maxPrintMessageLength - 1] = '\0';

		CommonMain::getTerminal()->info("Thread %u: %s", "User", tid, userMessage);
		CommonMain::getTerminal()->debug("Thread %u: %s", "User", tid, userMessage);
		//CommonMain::getTerminal()->printfBoth(true, "");

		return 0;
	}

	auto SyscallManager::syscallMMap(long *ret, const u64 hint, const u64 size, const u64 prot, const u64 flags, u64 fd, const u64 offset) -> u64 {
		(void) fd;

		// TODO: implement fd and offset

		if (ret == nullptr) {
			return EINVAL;
		}

		if (size == 0) {
			*ret = MAP_FAILED;

			return EINVAL;
		}

		auto *thread = Scheduler::getCurrentThread();
		auto *process = thread != nullptr ? thread->getParent() : nullptr;
		auto *scheduler = CommonMain::getInstance()->getScheduler();

		if (thread == nullptr || process == nullptr || scheduler == nullptr) {
			*ret = MAP_FAILED;

			return EFAULT;
		}

		AllocContext *ctx = process->getProcessContext();

		if (ctx == nullptr) {
			*ret = MAP_FAILED;

			return EFAULT;
		}

		u64 addr = hint;
		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		const bool fixed = static_cast<bool>(flags & MAP_FIXED);

		if (fixed && addr == 0) {
			scheduler->getSchedLock()->unlock(schedPrevIF);
			*ret = MAP_FAILED;

			return EINVAL;
		}

		if (static_cast<bool>(flags & MAP_ANON) and offset != 0) {
			scheduler->getSchedLock()->unlock(schedPrevIF);
			*ret = MAP_FAILED;

			return EINVAL;
		}

		if (addr == 0) {
			addr = pageSize;
		}

		if (size > ~0ULL - addr || addr + size > ~0ULL - (pageSize - 1)) {
			scheduler->getSchedLock()->unlock(schedPrevIF);
			*ret = MAP_FAILED;

			return EINVAL;
		}

		const u64 bottomAddr = alignDown<u64>(addr, pageSize);
		const u64 topAddr = alignUp<u64>(addr + size, pageSize);
		const u64 mapSize = topAddr - bottomAddr;

		if (!fixed) {
			if (hint != 0) {
				for (u64 i = bottomAddr; i < topAddr; i += pageSize) {
					if (ctx->pageMap.hasPageEntry(i)) {
						scheduler->getSchedLock()->unlock(schedPrevIF);
						*ret = MAP_FAILED;

						return EEXIST;
					}
				}
			} else {
				bool found = false;

				while (!found) {
					found = true;
					const u64 searchBottom = alignDown<u64>(addr, pageSize);
					const u64 searchTop = searchBottom + mapSize;

					if (searchTop < searchBottom) {
						CommonMain::getTerminal()->error("mmap failed: VA search wrapped pid=%u tid=%u addr=0x%.16lx size=0x%.16lx mapSize=0x%.16lx",
							"Syscall Manager", process->getId(), thread->getId(), addr, size, mapSize);
						scheduler->getSchedLock()->unlock(schedPrevIF);
						*ret = MAP_FAILED;

						return ENOMEM;
					}

					for (u64 i = searchBottom; i < searchTop; i += pageSize) {
						if (ctx->pageMap.hasPageEntry(i)) {
							if (searchTop > ~0ULL - pageSize) {
								CommonMain::getTerminal()->error("mmap failed: VA search exhausted pid=%u tid=%u addr=0x%.16lx size=0x%.16lx mapSize=0x%.16lx",
									"Syscall Manager", process->getId(), thread->getId(), addr, size, mapSize);
								scheduler->getSchedLock()->unlock(schedPrevIF);
								*ret = MAP_FAILED;

								return ENOMEM;
							}

							addr = searchTop;
							found = false;
							break;
						}
					}
				}
			}
		}

		const u64 finalBottomAddr = alignDown<u64>(addr, pageSize);
		const u64 finalTopAddr = finalBottomAddr + mapSize;

		if (fixed) {
			for (u64 i = finalBottomAddr; i < finalTopAddr; i += pageSize) {
				ctx->pageMap.unMapPage(i, true);
			}
		}

		const u8 pageFlags = static_cast<u8>((prot == PROT_NONE ? 0 : 1) | ((prot & PROT_WRITE) != 0 ? 0b10 : 0) | 0b100);

		for (u64 i = finalBottomAddr; i < finalTopAddr; i += pageSize) {
			const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (physPage == nullptr) {
				CommonMain::getTerminal()->error("mmap failed: PMM out of pages pid=%u tid=%u addr=0x%.16lx size=0x%.16lx free=0x%.16lx",
					"Syscall Manager", process->getId(), thread->getId(), finalBottomAddr, mapSize, CommonMain::getInstance()->getPMM()->getFreeMemory());
				for (u64 mapped = finalBottomAddr; mapped < i; mapped += pageSize) {
					ctx->pageMap.unMapPage(mapped, true);
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);
				*ret = MAP_FAILED;

				return ENOMEM;
			}

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(physPage), pageFlags, false, not static_cast<bool>(prot & PROT_EXEC));

			if (!ctx->pageMap.hasPageEntry(i)) {
				CommonMain::getTerminal()->error("mmap failed: page table insertion failed pid=%u tid=%u vaddr=0x%.16lx paddr=0x%.16lx free=0x%.16lx",
					"Syscall Manager", process->getId(), thread->getId(), i, reinterpret_cast<u64>(physPage), CommonMain::getInstance()->getPMM()->getFreeMemory());
				CommonMain::getInstance()->getPMM()->freePagesPhys(const_cast<u64 *>(physPage), 1);

				for (u64 mapped = finalBottomAddr; mapped < i; mapped += pageSize) {
					ctx->pageMap.unMapPage(mapped, true);
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);
				*ret = MAP_FAILED;

				return ENOMEM;
			}

			if (i > process->topmostMappedPage) {
				process->topmostMappedPage = i;
			}
		}

		*ret = static_cast<long>(addr);
		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallMUnmap(long */*unused*/, const u64 addr, const u64 size, const u64 freePage, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (size == 0 or addr == 0) {
			return EINVAL;
		}

		auto *thread = Scheduler::getCurrentThread();
		auto *process = thread != nullptr ? thread->getParent() : nullptr;
		auto *scheduler = CommonMain::getInstance()->getScheduler();

		if (thread == nullptr || process == nullptr || scheduler == nullptr) {
			return EFAULT;
		}

		if (addr > ~0ULL - size || addr + size > ~0ULL - (pageSize - 1)) {
			return EINVAL;
		}

		AllocContext *ctx = process->getProcessContext();
		const u64 bottomAddr = alignDown<u64>(addr, pageSize);
		const u64 topAddr = alignUp<u64>(addr + size, pageSize);
		const bool schedPrevIF = scheduler->getSchedLock()->lock();

		for (u64 i = bottomAddr; i < topAddr; i += pageSize) {
			const u64 phys = ctx->pageMap.getPhysAddress(i);
			UserPhysPage *tracked = findOwnedUserPhysPageUnlocked(process->getId(), alignDown<u64>(phys, pageSize));

			if (tracked != nullptr) {
				if (static_cast<bool>(freePage)) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					return EBUSY;
				}

				if (tracked != nullptr && tracked->mapCount > 0) {
					tracked->mapCount--;
				}
			}

			ctx->pageMap.unMapPage(i, static_cast<bool>(freePage));
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallGetTID(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		*ret = Scheduler::getCurrentThread()->getId();

		return 0;
	}

	auto SyscallManager::syscallArchCtl(long *ret, const u64 operation, const u64 pointer, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		//CommonMain::getTerminal()->debug("Arch specific syscall called with params: %lu 0x%.16lx", "Syscall Manager", operation, pointer);

		switch (operation) {
			case ARCH_CTL_SET_GSBASE:
				setGsBase(pointer);
				break;

			case ARCH_CTL_SET_FSBASE:
				setFsBase(pointer);
				break;

			case ARCH_CTL_GET_GSBASE:
				*ret = static_cast<long>(getGsBase());
				break;

			case ARCH_CTL_GET_FSBASE:
				*ret = static_cast<long>(getFsBase());
				break;

			default:
				return EFAULT;
		}

		return 0;
	}

	auto SyscallManager::syscallExit(long */*unused*/, const u64 status, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		CommonMain::getTerminal()->debug("Process %lu exited with status code: %lu", "Syscall", Scheduler::getCurrentThread()->getParent()->getId(), status);
		CommonMain::getTerminal()->info("Process %lu exited with status code: %lu", "Syscall", Scheduler::getCurrentThread()->getParent()->getId(), status);

		CommonMain::getInstance()->getScheduler()->killProcess(Scheduler::getCurrentThread()->getParent());

		return 0;
	}

	auto SyscallManager::syscallClockGet(long */*unused*/, const u64 clock, const u64 secs, const u64 nanos, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (!clockIdSupported(clock)) {
			return EINVAL;
		}

		const Thread *thread = Scheduler::getCurrentThread();
		const AllocContext *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		u64 clockNs = 0;
		const int err = getCurrentClockNs(&clockNs);

		if (err != 0) {
			return err;
		}

		const long clockSecs = static_cast<long>(clockNs / nanosecondsPerSecond);
		const long clockNanos = static_cast<long>(clockNs % nanosecondsPerSecond);

		int copyErr = copyToUser(ctx, secs, &clockSecs, sizeof(clockSecs));

		if (copyErr != 0) {
			return copyErr;
		}

		copyErr = copyToUser(ctx, nanos, &clockNanos, sizeof(clockNanos));

		if (copyErr != 0) {
			return copyErr;
		}

		return 0;
	}

	auto SyscallManager::syscallSysInfo(long */*unused*/, const u64 info, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *commonMain = CommonMain::getInstance();
		auto *scheduler = commonMain != nullptr ? commonMain->getScheduler() : nullptr;
		const Thread *thread = Scheduler::getCurrentThread();
		const auto *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (not isValidUserRange(ctx, info, sizeof(KernelSysInfo), true)) {
			return EFAULT;
		}

		u64 uptimeNs = 0;
		const int clockErr = getCurrentClockNs(&uptimeNs);

		if (clockErr != 0) {
			return clockErr;
		}

		// TODO
		KernelSysInfo kernelInfo {};
		kernelInfo.uptime = static_cast<long>(uptimeNs / nanosecondsPerSecond);
		kernelInfo.loads[0] = 0;
		kernelInfo.loads[1] = 0;
		kernelInfo.loads[2] = 0;
		kernelInfo.totalRam = CommonMain::getInstance()->getPMM()->getTotalMemory();
		kernelInfo.freeRam = commonMain != nullptr && commonMain->getPMM() != nullptr ? commonMain->getPMM()->getFreeMemory() : 0;
		kernelInfo.sharedRam = 0;
		kernelInfo.bufferRam = 0;
		kernelInfo.totalSwap = 0;
		kernelInfo.freeSwap = 0;
		kernelInfo.procs = countProcesses(scheduler);
		kernelInfo.totalHigh = 0;
		kernelInfo.freeHigh = 0;
		kernelInfo.memUnit = 1;

		const int copyErr = copyToUser(ctx, info, &kernelInfo, sizeof(kernelInfo));

		if (copyErr != 0) {
			return copyErr;
		}

		return 0;
	}

	auto SyscallManager::syscallKillThread(long */*unused*/, const u64 pid, const u64 tid, const u64 sig, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (sig == 0 or sig > signalActionCount or pid == 0 or tid == 0) {
			return EINVAL;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		if (scheduler == nullptr) {
			return EFAULT;
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		Thread *target = findThreadById(scheduler, pid, tid);

		if (target == nullptr) {
			scheduler->getSchedLock()->unlock(schedPrevIF);

			return ESRCH;
		}

		target->queueSignal(sig);
		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallPause(long */*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		return 0;
	}

	auto SyscallManager::syscallThreadExit(long */*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr) {
			return ESRCH;
		}

		scheduler->killThread(thread);

		return 0;
	}

	auto SyscallManager::syscallNewThread(long *ret, const u64 entryFun, const u64 stack, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		//CommonMain::getTerminal()->debug("New Thread: Stack: 0x%.16lx", "Syscall Manager", stack);

		auto *scheduler = CommonMain::getInstance()->getScheduler();
		const auto *currentThread = Scheduler::getCurrentThread();
		auto *process = currentThread != nullptr ? currentThread->getParent() : nullptr;

		if (scheduler == nullptr || process == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return ESRCH;
		}

		const auto *ctx = process->getProcessContext();

		if (entryFun == 0 || stack < sizeof(u64) || !isValidUserRange(ctx, entryFun, 1, false) || !isValidUserRange(ctx, stack - sizeof(u64), sizeof(u64), true)) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		auto *newThread = new Thread(scheduler, process, entryFun, true, 0, stack);

		if (newThread == nullptr) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return ENOMEM;
		}

		if (newThread->getContext() == nullptr) {
			delete newThread;

			if (ret != nullptr) {
				*ret = -1;
			}

			return ENOMEM;
		}

		newThread->setState(ThreadState::READY);

		const bool prevIF = scheduler->getSchedLock()->lock();
		process->addThread(newThread);
		scheduler->enqueueThread(newThread);
		scheduler->getSchedLock()->unlock(prevIF);

		if (ret != nullptr) {
			*ret = static_cast<long>(newThread->getId());
		}

		return 0;
	}

	auto SyscallManager::syscallSendMsg(long *ret, const u64 sendPort, const u64 port, const u64 msgHdr, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *scheduler = CommonMain::getInstance()->getScheduler();
		auto *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr or thread == nullptr or thread->getParent() == nullptr or msgHdr == 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return EFAULT;
		}

		const auto *ctx = thread->getParent()->getProcessContext();
		MessageHeader hdr {};
		const int hdrErr = copyFromUser(ctx, &hdr, msgHdr, sizeof(hdr));

		if (hdrErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return hdrErr;
		}

		auto *userBuffer = hdr.buffer;
		u8 *kernelBuffer = nullptr;

		if (hdr.length > 0) {
			kernelBuffer = new u8[hdr.length];

			if (kernelBuffer == nullptr) {
				if (ret != nullptr) {
					*ret = -1;
				}

				return ENOMEM;
			}

			const int bufferErr = copyFromUser(ctx, kernelBuffer, reinterpret_cast<u64>(userBuffer), hdr.length);

			if (bufferErr != 0) {
				delete[] kernelBuffer;

				if (ret != nullptr) {
					*ret = -1;
				}

				return bufferErr;
			}

			hdr.buffer = reinterpret_cast<u64 *>(kernelBuffer);
		}

		for (u64 retryAttempt = 0;; ++retryAttempt) {
			const u64 result = PortMessaging::sendMessage(sendPort, port, &hdr);

			if (result == 0) {
				hdr.buffer = userBuffer;
				const int copyErr = copyToUser(ctx, msgHdr, &hdr, sizeof(hdr));
				delete[] kernelBuffer;

				if (copyErr != 0) {
					if (ret != nullptr) {
						*ret = -1;
					}

					return copyErr;
				}

				if (ret != nullptr) {
					*ret = hdr.retLength;
				}

				return 0;
			}

			if (result != ENOENT) {
				delete[] kernelBuffer;

				if (ret != nullptr) {
					*ret = -1;
				}

				return result;
			}

			if (retryAttempt >= sendMessageRetryCount) {
				delete[] kernelBuffer;

				if (ret != nullptr) {
					*ret = -1;
				}

				return result;
			}

			CommonMain::getTerminal()->debug("Send message retry %lu for port %lu (source: %lu)", "Syscall Manager", retryAttempt + 1, port, sendPort);

			scheduler->sleepThread(thread, sendMessageRetrySleepMs * 1000000ULL);
		}
	}

	auto SyscallManager::syscallRecvMsg(long *ret, const u64 port, const u64 msgHdr, const u64 options, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		const auto *scheduler = CommonMain::getInstance()->getScheduler();
		const auto *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr or thread == nullptr or thread->getParent() == nullptr or msgHdr == 0) {
			return EFAULT;
		}

		const auto *ctx = thread->getParent()->getProcessContext();
		MessageHeader hdr {};
		const int hdrErr = copyFromUser(ctx, &hdr, msgHdr, sizeof(hdr));

		if (hdrErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return hdrErr;
		}

		CopiedMessageFilterOptions copiedOptions {};
		const int optionsErr = copyMessageFilterOptionsFromUser(ctx, options, &copiedOptions);

		if (optionsErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return optionsErr;
		}

		auto *userBuffer = hdr.buffer;
		u8 *kernelBuffer = nullptr;

		if (hdr.length > 0) {
			kernelBuffer = new u8[hdr.length];

			if (kernelBuffer == nullptr) {
				releaseCopiedMessageFilterOptions(&copiedOptions);

				if (ret != nullptr) {
					*ret = -1;
				}

				return ENOMEM;
			}

			hdr.buffer = reinterpret_cast<u64 *>(kernelBuffer);
		}

		const u64 result = PortMessaging::recvMessage(port, &hdr, options != 0 ? &copiedOptions.options : nullptr);

		if (result != 0) {
			delete[] kernelBuffer;
			releaseCopiedMessageFilterOptions(&copiedOptions);

			if (ret != nullptr) {
				*ret = -1;
			}

			return result;
		}

		const ssize retLength = hdr.retLength;

		if (retLength > 0) {
			if (static_cast<usize>(retLength) > hdr.length) {
				delete[] kernelBuffer;
				releaseCopiedMessageFilterOptions(&copiedOptions);

				if (ret != nullptr) {
					*ret = -1;
				}

				return EMSGSIZE;
			}

			const int bufferErr = copyToUser(ctx, reinterpret_cast<u64>(userBuffer), kernelBuffer, static_cast<usize>(retLength));

			if (bufferErr != 0) {
				delete[] kernelBuffer;
				releaseCopiedMessageFilterOptions(&copiedOptions);

				if (ret != nullptr) {
					*ret = -1;
				}

				return bufferErr;
			}
		}

		hdr.buffer = userBuffer;
		const int copyErr = copyToUser(ctx, msgHdr, &hdr, sizeof(hdr));

		delete[] kernelBuffer;
		releaseCopiedMessageFilterOptions(&copiedOptions);

		if (copyErr != 0) {
			if (ret != nullptr) {
				*ret = -1;
			}

			return copyErr;
		}

		if (ret != nullptr) {
			*ret = retLength;
		}

		return 0;
	}

	auto SyscallManager::syscallRegisterPort(long *ret, const u64 preferredPort, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (preferredPort == 0) {
			*ret = static_cast<long>(PortMessaging::getNewPort());
		} else {
			*ret = static_cast<long>(preferredPort);
		}

		return PortMessaging::registerPort(*ret);
	}

	auto SyscallManager::syscallFutex(long */*unused*/, const u64 pointer, const u64 type, const u64 expected, const u64 time, u64 /*unused*/, u64 /*unused*/) -> u64 {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (thread == nullptr or thread->getParent() == nullptr) {
			return EFAULT;
		}

		const AllocContext *ctx = thread->getParent()->getProcessContext();

		const u64 futexOp = type & FUTEX_CMD_MASK;
		const auto waitOnFutex = [&]() -> u64 {
			u64 timeoutNs = 0;
			const int timeoutErr = futexTimeoutFromUser(ctx, time, &timeoutNs);

			if (timeoutErr != 0) {
				return timeoutErr;
			}

			if (!isValidFutexPointer(ctx, pointer)) {
				return EFAULT;
			}

			const auto *futexWord = reinterpret_cast<volatile u32 *>(pointer);

			if (static_cast<u32>(*futexWord) != static_cast<u32>(expected)) {
				return EAGAIN;
			}

			const bool schedPrevIF = scheduler->getSchedLock()->lock();

			if (!isValidFutexPointer(ctx, pointer)) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return EFAULT;
			}

			if (static_cast<u32>(*reinterpret_cast<volatile u32 *>(pointer)) != static_cast<u32>(expected)) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return EAGAIN;
			}

			if (time != 0 && timeoutNs == 0) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return ETIMEDOUT;
			}

			u64 deadlineNs = 0;

			if (timeoutNs != 0) {
				deadlineNs = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

				if (deadlineNs > ~0ULL - timeoutNs) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					return EINVAL;
				}

				deadlineNs += timeoutNs;
			}

			if (!Futex::addWaiter(pointer, thread->getId())) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return ENOMEM;
			}

			thread->setSleepNs(deadlineNs);
			thread->setState(ThreadState::BLOCKED);
			scheduler->removeThread(thread);

			const Thread *currentEntry = Scheduler::getCurrentExecutionNode()->getCurrentThread();
			const bool shouldReschedule = currentEntry != nullptr && currentEntry == thread;

			scheduler->blockedThreadList.addStart(thread, false);

			if (deadlineNs != 0 && !scheduler->sleepingThreadList.contains(thread)) {
				scheduler->sleepingThreadList.addStart(thread, false);
			}

			if (shouldReschedule) {
				Scheduler::getCurrentExecutionNode()->setNextScheduleUnlockIF(schedPrevIF);
				Scheduler::getCurrentExecutionNode()->setSchedLockHeldForSwitch();
			} else {
				scheduler->getSchedLock()->unlock(schedPrevIF);
			}

			if (shouldReschedule) {
				ExecutionNode::reSchedule();
			}

			if (deadlineNs != 0 && Futex::removeWaiter(pointer, thread->getId())) {
				return ETIMEDOUT;
			}

			return 0;
		};

		switch (futexOp) {
			case FUTEX_WAIT: {
				return waitOnFutex();
			}

			case FUTEX_WAIT_BITSET: {
				return waitOnFutex();
			}

			case FUTEX_WAKE: {
				if (time != 0) {
					return EINVAL;
				}

				if (expected == 0) {
					return 0;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				u64 woken = 0;
				u16 threadId = 0;

				while (woken < expected && Futex::popWaiter(pointer, &threadId)) {
					Thread *waitThread = scheduler->getThread(threadId);

					if (waitThread != nullptr && waitThread->getState() == ThreadState::BLOCKED) {
						scheduler->blockedThreadList.remove(waitThread, false, false);
						scheduler->sleepingThreadList.remove(waitThread, false, false);
						waitThread->setState(ThreadState::RUNNING);
						waitThread->setSleepNs(0);
						scheduler->enqueueThread(waitThread, true);
						woken++;
					}
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);

				return 0;
			}

			case FUTEX_WAKE_BITSET: {
				if (time != 0) {
					return EINVAL;
				}

				if (expected == 0) {
					return 0;
				}

				const bool schedPrevIF = scheduler->getSchedLock()->lock();

				u64 woken = 0;
				u16 threadId = 0;

				while (woken < expected && Futex::popWaiter(pointer, &threadId)) {
					Thread *waitThread = scheduler->getThread(threadId);

					if (waitThread != nullptr && waitThread->getState() == ThreadState::BLOCKED) {
						scheduler->blockedThreadList.remove(waitThread, false, false);
						scheduler->sleepingThreadList.remove(waitThread, false, false);
						waitThread->setState(ThreadState::RUNNING);
						waitThread->setSleepNs(0);
						scheduler->enqueueThread(waitThread, true);
						woken++;
					}
				}

				scheduler->getSchedLock()->unlock(schedPrevIF);

				return 0;
			}

			default:
				return EINVAL;
		}
	}

	auto SyscallManager::syscallSigaction(long */*unused*/, const u64 sig, const u64 action, const u64 oldAction, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			return EFAULT;
		}

		if (sig == 0 || sig > signalActionCount) {
			return EINVAL;
		}

		constexpr u64 sigKill = 9;
		constexpr u64 sigStop = 19;

		if (action != 0 && (sig == sigKill || sig == sigStop)) {
			return EINVAL;
		}

		const AllocContext *ctx = thread->getParent()->getProcessContext();

		if (!isValidUserRange(ctx, action, action != 0 ? sizeof(SignalAction) : 0, false)) {
			if (action != 0) {
				return EFAULT;
			}
		}

		if (!isValidUserRange(ctx, oldAction, oldAction != 0 ? sizeof(SignalAction) : 0, true)) {
			if (oldAction != 0) {
				return EFAULT;
			}
		}

		SignalAction newActionCopy {};

		if (action != 0) {
			const int copyErr = copyFromUser(ctx, &newActionCopy, action, sizeof(newActionCopy));

			if (copyErr != 0) {
				return copyErr;
			}
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		const auto currentAction = thread->getParent()->signalActions[sig - 1];

		if (oldAction != 0) {
			scheduler->getSchedLock()->unlock(schedPrevIF);

			const int copyErr = copyToUser(ctx, oldAction, &currentAction, sizeof(currentAction));

			if (copyErr != 0) {
				return copyErr;
			}

			const bool relockPrevIF = scheduler->getSchedLock()->lock();

			if (action != 0) {
				thread->getParent()->signalActions[sig - 1] = newActionCopy;
			}

			scheduler->getSchedLock()->unlock(relockPrevIF);

			return 0;
		}

		if (action != 0) {
			thread->getParent()->signalActions[sig - 1] = newActionCopy;
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallSigreturn(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret != nullptr) {
			*ret = 0;
		}

		const Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
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

	auto SyscallManager::syscallMProtect(long */*unused*/, const u64 pointer, const u64 size, const u64 prot, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		//CommonMain::getTerminal()->debug("Memprotect: Prot: %lu, Size: %lu, Addr: 0x%.16lx", "Syscall Manager", prot, size, pointer);

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			return EFAULT;
		}

		if (pointer == 0 || size == 0) {
			return EINVAL;
		}

		constexpr u64 supportedProt = PROT_NONE | PROT_READ | PROT_WRITE | PROT_EXEC;
		if ((prot & ~supportedProt) != 0) {
			return EINVAL;
		}

		const u64 end = pointer + size - 1;
		if (end < pointer) {
			return EINVAL;
		}

		AllocContext *ctx = thread->getParent()->getProcessContext();
		const u64 bottomAddr = alignDown<u64>(pointer, pageSize);
		const u64 topAddr = alignUp<u64>(pointer + size, pageSize);
		const bool schedPrevIF = scheduler->getSchedLock()->lock();

		for (u64 addr = bottomAddr; addr < topAddr; addr += pageSize) {
			if (ctx->pageMap.getPhysAddress(addr) == 0) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return EFAULT;
			}
		}

		for (u64 addr = bottomAddr; addr < topAddr; addr += pageSize) {
			if (!ctx->pageMap.protectPage(addr, static_cast<u8>(prot))) {
				scheduler->getSchedLock()->unlock(schedPrevIF);

				return EFAULT;
			}
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallNanoSleep(long */*unused*/, const u64 secs, const u64 nanos, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr || thread == nullptr || thread->getParent() == nullptr) {
			return EFAULT;
		}

		const Timespec timeSpec = {
			.tv_sec = static_cast<long>(secs),
			.tv_nsec = static_cast<long>(nanos)
		};

		u64 sleepNs = 0;
		const int err = timespecToNs(&timeSpec, &sleepNs);
		if (err != 0) {
			return err;
		}

		if (sleepNs == 0) {
			return 0;
		}

		scheduler->sleepThread(thread, sleepNs);

		return 0;
	}

	auto SyscallManager::syscallIsaTTY(long */*unused*/, const u64 fd, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		(void) fd;

		// TODO: Unstub
		return 0;
	}

	auto SyscallManager::syscallKill(long */*unused*/, const u64 pid, const u64 signal, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		(void) signal;

		Scheduler *sched = CommonMain::getInstance()->getScheduler();

		sched->killProcess(sched->getProcess(pid));

		return 0;
	}

	auto SyscallManager::syscallGetPID(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		*ret = Scheduler::getCurrentThread()->getParent()->getId();

		return 0;
	}

	auto SyscallManager::syscallMMapPhys(long *ret, const u64 physAddr, const u64 len, const u64 isHhdm, const u64 cacheModeRaw, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		if (physAddr == 0 || len == 0) {
			return EINVAL;
		}

		if (static_cast<bool>(isHhdm)) {
			return EINVAL;
		}

		const auto *thread = Scheduler::getCurrentThread();
		auto *process = thread != nullptr ? thread->getParent() : nullptr;
		auto *kernelCtx = CommonMain::getInstance()->getKernelAllocContext();
		auto *processCtx = process != nullptr ? process->getProcessContext() : nullptr;
		auto *scheduler = CommonMain::getInstance()->getScheduler();

		if (thread == nullptr || process == nullptr || kernelCtx == nullptr || processCtx == nullptr || scheduler == nullptr) {
			return EFAULT;
		}

		const u64 alignedAddr = alignDown<u64>(physAddr, pageSize);
		const u64 pageOffset = physAddr - alignedAddr;

		if (len > ~0ULL - physAddr) {
			return EINVAL;
		}

		const u64 endAddr = physAddr + len;
		if (endAddr > ~0ULL - (pageSize - 1)) {
			return EINVAL;
		}

		const u64 alignedEnd = alignUp<u64>(endAddr, pageSize);
		auto cacheMode = PageCacheMode::WriteBack;

		switch (cacheModeRaw) {
			case MAP_CACHE_WB:
				cacheMode = PageCacheMode::WriteBack;
				break;

			case MAP_CACHE_WC:
				cacheMode = PageCacheMode::WriteCombining;
				break;

			case MAP_CACHE_UC:
				cacheMode = PageCacheMode::Uncacheable;
				break;

			case MAP_CACHE_WT:
				cacheMode = PageCacheMode::WriteThrough;
				break;

			default:
				return EINVAL;
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		const u64 retAddr = process->topmostMappedPage + pageSize + pageOffset;
		const u64 originalTopmostMappedPage = process->topmostMappedPage;
		u64 mappedBytes = 0;
		u64 countedBytes = 0;

		const auto rollbackMappings = [&]() {
			u64 virt = retAddr - pageOffset;

			for (u64 offset = 0; offset < mappedBytes; offset += pageSize) {
				kernelCtx->pageMap.unMapPage(virt + offset, false);
				processCtx->pageMap.unMapPage(virt + offset, false);
			}

			for (u64 offset = 0; offset < countedBytes; offset += pageSize) {
				UserPhysPage *tracked = findOwnedUserPhysPageUnlocked(process->getId(), alignedAddr + offset);

				if (tracked != nullptr && tracked->mapCount > 0) {
					tracked->mapCount--;
				}
			}

			process->topmostMappedPage = originalTopmostMappedPage;
		};

		for (u64 i = alignedAddr; i < alignedEnd;) {
			const u64 virtAddr = process->topmostMappedPage + pageSize;
			const u64 remaining = alignedEnd - i;
			u64 mappingSize = pageSize;
			bool mapped = false;

			const auto tryMapHuge = [&](const u64 hugeSize) {
				if (!kernelCtx->pageMap.mapHugePage(virtAddr, i, hugeSize, 0b00000111, false, false, cacheMode)) {
					return false;
				}

				if (!processCtx->pageMap.mapHugePage(virtAddr, i, hugeSize, 0b00000111, false, false, cacheMode)) {
					kernelCtx->pageMap.unMapPage(virtAddr, false);
					return false;
				}

				mappingSize = hugeSize;
				mapped = true;
				return true;
			};

			if (remaining >= hugePageSize1GiB && (i & (hugePageSize1GiB - 1)) == 0 && (virtAddr & (hugePageSize1GiB - 1)) == 0) {
				tryMapHuge(hugePageSize1GiB);
			}

			if (!mapped && remaining >= hugePageSize2MiB && (i & (hugePageSize2MiB - 1)) == 0 && (virtAddr & (hugePageSize2MiB - 1)) == 0) {
				tryMapHuge(hugePageSize2MiB);
			}

			if (!mapped) {
				kernelCtx->pageMap.mapPage(virtAddr, i, 0b00000111, false, false, cacheMode);
				processCtx->pageMap.mapPage(virtAddr, i, 0b00000111, false, false, cacheMode);
				mappingSize = pageSize;
			}

			if (virtAddr > process->topmostMappedPage) {
				process->topmostMappedPage = virtAddr + mappingSize - pageSize;
			}

			if (processCtx->pageMap.getPhysAddress(virtAddr) == 0) {
				kernelCtx->pageMap.unMapPage(virtAddr, false);
				processCtx->pageMap.unMapPage(virtAddr, false);
				rollbackMappings();
				scheduler->getSchedLock()->unlock(schedPrevIF);
				return ENOMEM;
			}

			mappedBytes += mappingSize;

			for (u64 page = i; page < i + mappingSize; page += pageSize) {
				UserPhysPage *tracked = findOwnedUserPhysPageUnlocked(process->getId(), page);

				if (tracked != nullptr) {
					tracked->mapCount++;
				}
			}

			countedBytes += mappingSize;
			i += mappingSize;
		}

		*ret = static_cast<long>(retAddr);
		scheduler->getSchedLock()->unlock(schedPrevIF);

		return 0;
	}

	auto SyscallManager::syscallGetRsdp(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		*ret = static_cast<long>(CommonMain::getCurrentRsdp() - CommonMain::getCurrentHhdm());

		return 0;
	}

	auto SyscallManager::syscallLockToCore(long */*unused*/, const u64 cpuId, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		const auto *scheduler = CommonMain::getInstance()->getScheduler();
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler == nullptr or thread == nullptr) {
			return EFAULT;
		}

		if (mpRequest.response == nullptr) {
			return EINVAL;
		}

		const ExecutionNode *targetNode = Scheduler::getCoreEN(cpuId);

		if (targetNode == nullptr) {
			return EINVAL;
		}

		thread->setLockedCoreId(cpuId);

		if (targetNode == Scheduler::getCurrentExecutionNode()) {
			return 0;
		}

		ExecutionNode::reSchedule();

		return 0;
	}

	auto SyscallManager::syscallGetCpuIDs(long */*unused*/, const u64 cpuIdOutArray, const u64 cpuCount, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (cpuIdOutArray == 0) {
			return EINVAL;
		}

		if (mpRequest.response == nullptr) {
			return EFAULT;
		}

		if (cpuCount > mpRequest.response->cpu_count) {
			return EINVAL;
		}

		if (cpuCount > (~static_cast<u64>(0) / sizeof(HosCpuInfo))) {
			return EINVAL;
		}

		const Thread *thread = Scheduler::getCurrentThread();
		const auto *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (!isFullyMappedUserRange(ctx, cpuIdOutArray, cpuCount * sizeof(HosCpuInfo), true)) {
			return EFAULT;
		}

		for (u64 i = 0; i < cpuCount; i++) {
			HosCpuInfo info {};
			info.cpuId = mpRequest.response->cpus[i]->processor_id;
			info.apicId = mpRequest.response->cpus[i]->lapic_id;

			const int err = copyToUser(ctx, cpuIdOutArray + (i * sizeof(info)), &info, sizeof(info));

			if (err != 0) {
				return err;
			}
		}

		return 0;
	}

	auto SyscallManager::syscallAllocPhysPage(long *ret, const u64 maxPhysExclusive, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		auto *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();
		const Process *process = thread != nullptr ? thread->getParent() : nullptr;

		if (scheduler == nullptr || process == nullptr) {
			return EFAULT;
		}

		const u64 *page = maxPhysExclusive == 0
			? CommonMain::getInstance()->getPMM()->allocPages(1, false)
			: CommonMain::getInstance()->getPMM()->allocPagesBelow(1, false, maxPhysExclusive);

		if (page == nullptr) {
			return ENOMEM;
		}

		auto *trackedPage = new UserPhysPage();

		if (trackedPage == nullptr) {
			CommonMain::getInstance()->getPMM()->freePagesPhys(page, 1);

			return ENOMEM;
		}

		trackedPage->address = reinterpret_cast<u64>(page);
		trackedPage->ownerPid = process->getId();

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		userPhysPages.addEnd(trackedPage, false);
		scheduler->getSchedLock()->unlock(schedPrevIF);

		*ret = reinterpret_cast<long>(page);

		return 0;
	}

	auto SyscallManager::syscallFreePhysPage(long */*unused*/, const u64 pageAddr, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (pageAddr == 0 || (pageAddr & (pageSize - 1)) != 0) {
			return EINVAL;
		}

		auto *scheduler = CommonMain::getInstance()->getScheduler();
		const Thread *thread = Scheduler::getCurrentThread();
		const Process *process = thread != nullptr ? thread->getParent() : nullptr;

		if (scheduler == nullptr || process == nullptr) {
			return EFAULT;
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		bool owned = false;

		auto *node = userPhysPages.getFirst();

		while (node != nullptr) {
			auto *next = node->next;

			if (node->value != nullptr && node->value->address == pageAddr && node->value->ownerPid == process->getId()) {
				if (node->value->mapCount != 0) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					return EBUSY;
				}

				owned = userPhysPages.removeEntry(node, false);

				if (owned) {
					delete node->value;
					delete node;
				}

				break;
			}

			node = next;
		}

		scheduler->getSchedLock()->unlock(schedPrevIF);

		if (!owned) {
			return EPERM;
		}

		CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(pageAddr), 1);

		return 0;
	}

	auto SyscallManager::syscallGetAffinity(long */*unused*/, const u64 tidPid, const u64 cpuSetSize, const u64 mask, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (cpuSetSize == 0) {
			return EINVAL;
		}

		const Thread *currentThread = Scheduler::getCurrentThread();

		if (currentThread == nullptr or currentThread->getParent() == nullptr) {
			return EFAULT;
		}

		const AllocContext *ctx = currentThread->getParent()->getProcessContext();

		if (!isValidUserRange(ctx, mask, cpuSetSize, true)) {
			return EFAULT;
		}

		u64 onlineCpuCount = 0;
		u64 maxCpuId = 0;

		int err = getOnlineCpuInfo(&onlineCpuCount, &maxCpuId);

		if (err != 0) {
			return err;
		}

		if (maxCpuId / affinityBitsPerByte >= cpuSetSize) {
			return EINVAL;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		if (scheduler == nullptr) {
			return EFAULT;
		}

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		const Thread *targetThread = findAffinityTarget(scheduler, tidPid);

		if (targetThread == nullptr) {
			scheduler->getSchedLock()->unlock(schedPrevIF);

			return ESRCH;
		}

		const u64 lockedCoreId = targetThread->getLockedCoreId();
		scheduler->getSchedLock()->unlock(schedPrevIF);

		for (usize i = 0; i < cpuSetSize; i++) {
			err = writeUserByte(ctx, mask + i, 0);

			if (err != 0) {
				return err;
			}
		}

		if (lockedCoreId != unlockedAffinityCpuId) {
			return setUserCpuMaskBit(ctx, mask, cpuSetSize, lockedCoreId);
		}

		for (u64 i = 0; i < mpRequest.response->cpu_count; i++) {
			const limine_mp_info *cpuInfo = mpRequest.response->cpus[i];
			if (cpuInfo == nullptr) {
				continue;
			}

			const u64 cpuId = cpuInfo->processor_id;
			if (Scheduler::getCoreEN(cpuId) == nullptr) {
				continue;
			}

			err = setUserCpuMaskBit(ctx, mask, cpuSetSize, cpuId);
			if (err != 0) {
				return err;
			}
		}

		return 0;
	}

	auto SyscallManager::syscallSetAffinity(long */*unused*/, const u64 tidPid, const u64 cpuSetSize, const u64 mask, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (cpuSetSize == 0) {
			return EINVAL;
		}

		const Thread *currentThread = Scheduler::getCurrentThread();

		if (currentThread == nullptr or currentThread->getParent() == nullptr) {
			return EFAULT;
		}

		const AllocContext *ctx = currentThread->getParent()->getProcessContext();

		if (!isValidUserRange(ctx, mask, cpuSetSize, false)) {
			return EFAULT;
		}

		u64 onlineCpuCount = 0;
		u64 maxCpuId = 0;

		int err = getOnlineCpuInfo(&onlineCpuCount, &maxCpuId);

		if (err != 0) {
			return err;
		}

		if (maxCpuId / affinityBitsPerByte >= cpuSetSize) {
			return EINVAL;
		}

		u64 selectedOnlineCpuCount = 0;
		u64 selectedCpuId = 0;

		for (u64 i = 0; i < mpRequest.response->cpu_count; i++) {
			const limine_mp_info *cpuInfo = mpRequest.response->cpus[i];

			if (cpuInfo == nullptr) {
				continue;
			}

			const u64 cpuId = cpuInfo->processor_id;

			if (Scheduler::getCoreEN(cpuId) == nullptr) {
				continue;
			}

			bool isSet = false;
			err = getUserCpuMaskBit(ctx, mask, cpuSetSize, cpuId, &isSet);

			if (err != 0) {
				return err;
			}

			if (!isSet) {
				continue;
			}

			if (selectedOnlineCpuCount == 0) {
				selectedCpuId = cpuId;
			}

			selectedOnlineCpuCount++;
		}

		if (selectedOnlineCpuCount == 0) {
			return EINVAL;
		}

		const bool unlockAffinity = selectedOnlineCpuCount == onlineCpuCount;

		if (!unlockAffinity && selectedOnlineCpuCount != 1) {
			return EINVAL;
		}

		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		if (scheduler == nullptr) {
			return EFAULT;
		}

		const u64 newLockedCoreId = unlockAffinity ? unlockedAffinityCpuId : selectedCpuId;
		bool shouldReschedule = false;

		const bool schedPrevIF = scheduler->getSchedLock()->lock();
		Thread *targetThread = findAffinityTarget(scheduler, tidPid);

		if (targetThread == nullptr) {
			scheduler->getSchedLock()->unlock(schedPrevIF);

			return ESRCH;
		}

		targetThread->setLockedCoreId(newLockedCoreId);

		if (targetThread->isQueued()) {
			scheduler->enqueueThread(targetThread);
		}

		shouldReschedule = targetThread == currentThread and newLockedCoreId != unlockedAffinityCpuId and Scheduler::getCoreEN(newLockedCoreId) != Scheduler::getCurrentExecutionNode();

		scheduler->getSchedLock()->unlock(schedPrevIF);

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}

		return 0;
	}

	auto SyscallManager::syscallRegisterEventHandler(long */*unused*/, const u64 port, const u64 eventMask, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (port == 0 or eventMask == 0) {
			return EINVAL;
		}

		for (auto &registration : eventRegistrations) {
			if (registration.port == port) {
				registration.eventMask = eventMask;

				return 0;
			}
		}

		auto *registration = new KernelEventRegistration();

		if (registration == nullptr) {
			return ENOMEM;
		}

		registration->port = port;
		registration->eventMask = eventMask;
		eventRegistrations.addEnd(registration);

		return 0;
	}

	auto SyscallManager::syscallGetFramebufferInfo(long */*unused*/, const u64 infoOut, const u64 framebufferIndex, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		const Thread *thread = Scheduler::getCurrentThread();
		const AllocContext *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (ctx == nullptr || infoOut == 0) {
			return EFAULT;
		}

		if (framebufferRequest.response == nullptr || framebufferIndex >= framebufferRequest.response->framebuffer_count) {
			return ENOENT;
		}

		const limine_framebuffer *fb = framebufferRequest.response->framebuffers[framebufferIndex];

		if (fb == nullptr || fb->address == nullptr || fb->pitch == 0 || fb->height == 0) {
			return EFAULT;
		}

		const u64 fbVirt = reinterpret_cast<u64>(fb->address);
		const u64 fbPhys = CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPhysAddress(fbVirt);

		if (fbPhys == 0) {
			return EFAULT;
		}

		FramebufferInfo info {};

		info.physicalAddress = fbPhys;
		info.length = fb->pitch * fb->height;
		info.width = fb->width;
		info.height = fb->height;
		info.pitch = fb->pitch;
		info.bpp = fb->bpp;
		info.redMaskSize = fb->red_mask_size;
		info.redMaskShift = fb->red_mask_shift;
		info.greenMaskSize = fb->green_mask_size;
		info.greenMaskShift = fb->green_mask_shift;
		info.blueMaskSize = fb->blue_mask_size;
		info.blueMaskShift = fb->blue_mask_shift;

		return copyToUser(ctx, infoOut, &info, sizeof(info));
	}

	auto SyscallManager::syscallReadKernelLog(long *ret, const u64 afterSequence, const u64 entriesOut, const u64 maxEntries, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		const Thread *thread = Scheduler::getCurrentThread();
		const AllocContext *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (ret == nullptr) {
			return EINVAL;
		}

		*ret = 0;

		if (ctx == nullptr || entriesOut == 0 || maxEntries == 0) {
			return EFAULT;
		}

		constexpr usize maxBatch = 64;
		const usize requested = maxEntries > maxBatch ? maxBatch : maxEntries;
		KernelLogEntry entries[maxBatch] {};
		const usize copied = CommonMain::getTerminal()->readInfoLog(afterSequence, entries, requested);

		if (copied == 0) {
			return 0;
		}

		const int err = copyToUser(ctx, entriesOut, entries, copied * sizeof(KernelLogEntry));

		if (err != 0) {
			return err;
		}

		*ret = static_cast<long>(copied);

		return 0;
	}
}
