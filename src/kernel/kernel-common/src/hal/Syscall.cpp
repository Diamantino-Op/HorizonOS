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
		constexpr u64 unlockedAffinityCpuId = ~0x0U;
		constexpr usize affinityBitsPerByte = 8;
		constexpr usize maxPrintMessageLength = 1024;

		auto isMappedAddress(const AllocContext *ctx, const u64 addr) -> bool {
			return ctx->pageMap.getPhysAddress(addr) != 0;
		}

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
			if (ctx == nullptr or userPtr == 0) {
				return EFAULT;
			}

			if (!isValidUserRange(ctx, userPtr, size)) {
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

			if (!isValidUserRange(ctx, userPtr, size)) {
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
		horizonSyscalls[37] = &syscallGetCpuIDs;
		horizonSyscalls[38] = &syscallAllocPhysPage;
		horizonSyscalls[39] = &syscallFreePhysPage;
		horizonSyscalls[40] = &syscallGetAffinity;
		horizonSyscalls[41] = &syscallSetAffinity;

		initArch();
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

				schedulerPtr->blockedThreadList.remove(&currEntry, false);
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

		if (size == 0) {
			*ret = MAP_FAILED;

			return EINVAL;
		}

		AllocContext *ctx = Scheduler::getCurrentThread()->getParent()->getProcessContext();

		u64 addr = hint;

		if (addr == 0) {
			addr = Scheduler::getCurrentThread()->getParent()->topmostMappedPage + pageSize;
		} else if (ctx->pageMap.getPhysAddress(addr) != 0) {
			*ret = MAP_FAILED;

			return EEXIST;
		}

		if (static_cast<bool>(flags & MAP_ANON) and offset != 0) {
			*ret = MAP_FAILED;

			return EINVAL;
		}

		if (size > ~0ULL - addr || addr + size > ~0ULL - (pageSize - 1)) {
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

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(physPage), (prot & 0b11) | 0b101, false, not static_cast<bool>(prot & PROT_EXEC));

			if (i > Scheduler::getCurrentThread()->getParent()->topmostMappedPage) {
				Scheduler::getCurrentThread()->getParent()->topmostMappedPage = i;
			}
		}

		*ret = static_cast<long>(addr);

		return 0;
	}

	auto SyscallManager::syscallMUnmap(long */*unused*/, const u64 addr, const u64 size, const u64 freePage, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (size == 0 or addr == 0) {
			return EINVAL;
		}

		AllocContext *ctx = Scheduler::getCurrentThread()->getParent()->getProcessContext();

		const u64 bottomAddr = alignDown<u64>(addr, pageSize);
		const u64 topAddr = alignUp<u64>(addr + size, pageSize);

		for (u64 i = bottomAddr; i < topAddr; i += pageSize) {
			ctx->pageMap.unMapPage(i, static_cast<bool>(freePage));
		}

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

		u64 clockNs = 0;
		const int err = getCurrentClockNs(&clockNs);

		if (err != 0) {
			return err;
		}

		*reinterpret_cast<long *>(secs) = static_cast<long>(clockNs / nanosecondsPerSecond);
		*reinterpret_cast<long *>(nanos) = static_cast<long>(clockNs % nanosecondsPerSecond);

		return 0;
	}

	auto SyscallManager::syscallSysInfo(long */*unused*/, const u64 info, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *commonMain = CommonMain::getInstance();
		auto *scheduler = commonMain != nullptr ? commonMain->getScheduler() : nullptr;
		const Thread *thread = Scheduler::getCurrentThread();
		const auto *ctx = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getProcessContext() : nullptr;

		if (not isValidUserRange(ctx, info, sizeof(KernelSysInfo))) {
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
		kernelInfo.totalRam = getUsableMemoryBytes();
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

		auto *newThread = new Thread(scheduler, process, entryFun, true, 0, stack);
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

			CommonMain::getTerminal()->debug("Send message retry %lu for port %lu (source: %lu)", "Syscall Manager", retryAttempt + 1, port, sendPort);

			scheduler->sleepThread(thread, sendMessageRetrySleepMs * 1000000ULL);
		}
	}

	auto SyscallManager::syscallRecvMsg(long *ret, const u64 port, const u64 msgHdr, const u64 options, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		auto *hdr = reinterpret_cast<MessageHeader *>(msgHdr);
		const auto *scheduler = CommonMain::getInstance()->getScheduler();
		const auto *thread = Scheduler::getCurrentThread();

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

		switch (futexOp) {
			case FUTEX_WAIT: {
				if (time != 0) {
					return EINVAL;
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

				if (!Futex::addWaiter(pointer, thread->getId())) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					return ENOMEM;
				}

				thread->setState(ThreadState::BLOCKED);
				scheduler->removeThread(thread);

				const Thread *currentEntry = Scheduler::getCurrentExecutionNode()->getCurrentThread();
				const bool shouldReschedule = currentEntry != nullptr && currentEntry == thread;

				scheduler->blockedThreadList.addStart(thread);

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (shouldReschedule) {
					ExecutionNode::reSchedule();
				}

				return 0;
			}

			case FUTEX_WAIT_BITSET: {
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

				if (!Futex::addWaiter(pointer, thread->getId())) {
					scheduler->getSchedLock()->unlock(schedPrevIF);

					return ENOMEM;
				}

				thread->setState(ThreadState::BLOCKED);
				scheduler->removeThread(thread);

				const Thread *currentEntry = Scheduler::getCurrentExecutionNode()->getCurrentThread();
				const bool shouldReschedule = currentEntry != nullptr && currentEntry == thread;

				scheduler->blockedThreadList.addStart(thread);

				scheduler->getSchedLock()->unlock(schedPrevIF);

				if (shouldReschedule) {
					ExecutionNode::reSchedule();
				}

				return 0;
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
						scheduler->blockedThreadList.remove(waitThread, false);
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
						scheduler->blockedThreadList.remove(waitThread, false);
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

		if (!isValidUserRange(ctx, action, action != 0 ? sizeof(SignalAction) : 0)) {
			if (action != 0) {
				return EFAULT;
			}
		}

		if (!isValidUserRange(ctx, oldAction, oldAction != 0 ? sizeof(SignalAction) : 0)) {
			if (oldAction != 0) {
				return EFAULT;
			}
		}

		SignalAction newActionCopy;

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

		const auto *thread = Scheduler::getCurrentThread();
		const auto *process = thread != nullptr ? thread->getParent() : nullptr;
		auto *kernelCtx = CommonMain::getInstance()->getKernelAllocContext();
		auto *processCtx = process != nullptr ? process->getProcessContext() : nullptr;

		if (thread == nullptr || process == nullptr || kernelCtx == nullptr || processCtx == nullptr) {
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

		const u64 roundedLen = alignUp<u64>(endAddr, pageSize);
		const u64 hhdmBase = CommonMain::getCurrentHhdm();
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

		// TODO: Check if it should be used before re-mapping it

		u64 retAddr = 0;

		if (static_cast<bool>(isHhdm)) {
			retAddr = physAddr + hhdmBase;
		} else {
			retAddr = Scheduler::getCurrentThread()->getParent()->topmostMappedPage + pageSize + pageOffset;
		}

		for (u64 i = alignedAddr; i < roundedLen;) {
			u64 virtAddr = 0;

			if (static_cast<bool>(isHhdm)) {
				virtAddr = i + hhdmBase;
			} else {
				virtAddr = Scheduler::getCurrentThread()->getParent()->topmostMappedPage + pageSize;
			}

			const u64 remaining = roundedLen - i;
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
			}

			if (static_cast<bool>(isHhdm)) {
				PageMap::invPg(virtAddr);
			} else if (virtAddr > Scheduler::getCurrentThread()->getParent()->topmostMappedPage) {
				Scheduler::getCurrentThread()->getParent()->topmostMappedPage = virtAddr + mappingSize - pageSize;
			}

			i += mappingSize;
		}

		*ret = static_cast<long>(retAddr);

		return 0;
	}

	auto SyscallManager::syscallGetRsdp(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
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
		auto *cpuIdArr = reinterpret_cast<HosCpuInfo *>(cpuIdOutArray);

		if (cpuIdArr == nullptr) {
			return EINVAL;
		}

		if (mpRequest.response == nullptr) {
			return EFAULT;
		}

		if (cpuCount > mpRequest.response->cpu_count) {
			return EINVAL;
		}

		for (u64 i = 0; i < cpuCount; i++) {
			cpuIdArr[i].cpuId = mpRequest.response->cpus[i]->processor_id;
			cpuIdArr[i].apicId = mpRequest.response->cpus[i]->lapic_id;
		}

		return 0;
	}

	auto SyscallManager::syscallAllocPhysPage(long *ret, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (ret == nullptr) {
			return EINVAL;
		}

		const u64 *page = CommonMain::getInstance()->getPMM()->allocPages(1, false);

		if (page == nullptr) {
			return ENOMEM;
		}

		*ret = reinterpret_cast<long>(page);

		return 0;
	}

	auto SyscallManager::syscallFreePhysPage(long */*unused*/, const u64 pageAddr, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/, u64 /*unused*/) -> u64 {
		if (pageAddr == 0 || (pageAddr & (pageSize - 1)) != 0) {
			return EINVAL;
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

		if (!isValidUserRange(ctx, mask, cpuSetSize)) {
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

		if (!isValidUserRange(ctx, mask, cpuSetSize)) {
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
}
