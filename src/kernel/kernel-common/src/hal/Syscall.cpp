#include "Syscall.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "Math.hpp"

namespace kernel::common::hal {
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

		initArch();
	}

	u64 SyscallManager::syscallPrint(long *, const u64 message, u64, u64, u64, u64, u64) {
		CommonMain::getTerminal()->info(reinterpret_cast<char *>(message), "User");

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

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(physPage), (prot & 0b11) | 0b101, false, not (prot & PROT_EXEC));

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

	u64 SyscallManager::syscallNewThread(long *ret, u64 entryFun, u64 stack, u64, u64, u64, u64) {
		return 0;
	}
} // namespace kernel::common::hal