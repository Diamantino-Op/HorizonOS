#ifndef KERNEL_COMMON_SYSCALL_HPP
#define KERNEL_COMMON_SYSCALL_HPP

#include "LinkedList.hpp"
#include "Types.hpp"
#include "memory/VirtualAllocator.hpp"

#define ARCH_CTL_SET_GSBASE 0
#define ARCH_CTL_SET_FSBASE 1
#define ARCH_CTL_GET_GSBASE 2
#define ARCH_CTL_GET_FSBASE 3

#define MAP_FAILED (-1)
#define MAP_FILE    0x00
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS 0x20

#define MAP_GROWSDOWN 0x100
#define MAP_DENYWRITE 0x800
#define MAP_EXECUTABLE 0x1000
#define MAP_LOCKED    0x2000
#define MAP_NORESERVE 0x4000
#define MAP_POPULATE  0x8000
#define MAP_NONBLOCK  0x10000
#define MAP_STACK     0x20000
#define MAP_HUGETLB   0x40000
#define MAP_SYNC      0x80000
#define MAP_FIXED_NOREPLACE 0x100000

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_CACHE_WB 0
#define MAP_CACHE_WC 1
#define MAP_CACHE_UC 2
#define MAP_CACHE_WT 3

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 0x80
#define FUTEX_CMD_MASK 0x7f
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAIT_BITSET_PRIVATE (FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_BITSET_PRIVATE (FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG)

namespace kernel::common::hal {
	using namespace memory;

	using SyscallFun = u64(*)(long *ret, u64 p1, u64 p2, u64 p3, u64 p4, u64 p5, u64 p6);

	constexpr u64 nanosecondsPerSecond = 1'000'000'000ULL;

	constexpr u64 sendMessageRetrySleepMs = 50;
	constexpr u64 sendMessageRetryCount = 40;

	constexpr u64 linuxSyscallAmount = 309;
	constexpr u64 horizonSyscallAmount = 45;

	constexpr u64 irqReceiveMsgType = 0x1000;
	constexpr u64 kernelEventMsgType = 0x1100;
	constexpr u64 kernelEventThreadKilled = 1;
	constexpr u64 kernelEventProcessKilled = 2;

	struct KernelSysInfo {
		long uptime;
		u64 loads[3];
		u64 totalRam;
		u64 freeRam;
		u64 sharedRam;
		u64 bufferRam;
		u64 totalSwap;
		u64 freeSwap;
		u16 procs;
		u64 totalHigh;
		u64 freeHigh;
		u32 memUnit;
	};

	struct Timespec {
		long tv_sec;
		long tv_nsec;
	};

	struct MessageHeader {
		u64 port;               /* target port (for send) or port that received the message */
		u64 type;				/* message type used for filtering */
		u64 *buffer;            /* pointer to message buffer in user space */
		usize length;           /* buffer length */
		int flags;              /* message flags (e.g. MSG_DONTWAIT) */
		ssize retLength;        /* kernel-filled: number of bytes sent/received (or negative error) */
		u64 srcPort;            /* set by kernel on receive: source port */
		void *control;          /* optional ancillary/control data pointer */
		usize controlLen;       /* ancillary data length */
		u64 timeoutNs;		    /* optional timeout in nanoseconds (0 = wait indefinitely) */
	};

	struct MessageFilterOptions {
		u64 *blackListTypes;    /* pointer to array of message types to block */
		usize blackListCount;   /* number of entries in blackListTypes */
		u64 *whiteListTypes;    /* pointer to array of message types to allow (if blackListTypes is not used) */
		usize whiteListCount;   /* number of entries in whiteListTypes */
	};

	struct IrqRegistration {
		u64 irq;
		u64 port;
		u64 destCpu;
		bool isIrq;
		bool isLapicDest;
	};

	struct IrqReceiveData {
		u64 irqNum {};
		u64 cpuId {};
		bool isIrq {};
	};

	struct KernelEventRegistration {
		u64 port {};
		u64 eventMask {};
	};

	struct KernelEventData {
		u64 eventType {};
		u64 pid {};
		u64 tid {};
	};

	struct HosCpuInfo {
		u64 cpuId;
		u64 apicId;
	};

    class SyscallManager {
    public:
        static void init();
    	static void initArch();

    	// HorizonOS / Linux syscalls
		static auto syscallPrint(long *, u64 message, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallMMap(long *ret, u64 hint, u64 size, u64 prot, u64 flags, u64 fd, u64 offset) -> u64;
    	static auto syscallMUnmap(long *, u64 addr, u64 size, u64 freePage, u64, u64, u64) -> u64;
    	static auto syscallGetTID(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallArchCtl(long *ret, u64 operation, u64 pointer, u64, u64, u64, u64) -> u64;
    	static auto syscallExit(long *, u64 status, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallClockGet(long *, u64 clock, u64 secs, u64 nanos, u64, u64, u64) -> u64;
    	static auto syscallSysInfo(long *, u64 info, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallGetCpu(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallKillThread(long *, u64 pid, u64 tid, u64 sig, u64, u64, u64) -> u64;
    	static auto syscallPause(long *, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallThreadExit(long *, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallNewThread(long *ret, u64 entryFun, u64 stack, u64, u64, u64, u64) -> u64;
    	static auto syscallSendMsg(long *ret, u64 sendPort, u64 port, u64 msgHdr, u64, u64, u64) -> u64;
    	static auto syscallRecvMsg(long *ret, u64 port, u64 msgHdr, u64 options, u64, u64, u64) -> u64;
    	static auto syscallRegisterPort(long *ret, u64 preferredPort, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallIsThreadAlive(long *, u64 tid, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallFutex(long *, u64 pointer, u64 type, u64 expected, u64 time, u64, u64) -> u64;
        static auto syscallSigreturn(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallSigaction(long *, u64 sig, u64 action, u64 oldAction, u64, u64, u64) -> u64;
    	static auto syscallMProtect(long *, u64 pointer, u64 size, u64 prot, u64, u64, u64) -> u64;
    	static auto syscallNanoSleep(long *, u64 secs, u64 nanos, u64, u64, u64, u64) -> u64;
    	static auto syscallIsaTTY(long *, u64 fd, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallIoPerm(long *, u64 from, u64 num, u64 state, u64, u64, u64) -> u64;
    	static auto syscallIoPl(long *, u64 level, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallKill(long *, u64 pid, u64 signal, u64, u64, u64, u64) -> u64;
    	static auto syscallGetPID(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
		static auto syscallMMapPhys(long *ret, u64 physAddr, u64 len, u64 isHhdm, u64 cacheModeRaw, u64, u64) -> u64;
    	static auto syscallGetRsdp(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallInstallIRQHandler(long *ret, u64 irq, u64 port, u64, u64, u64, u64) -> u64;
    	static auto syscallUninstallIRQHandler(long *ret, u64 irq, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallGetIRQMode(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallSetIntStatus(long *ret, u64 status, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallAllocIntVec(long *ret, u64 port, u64 destCpu, u64 isLapic, u64, u64, u64) -> u64;
    	static auto syscallFreeIntVec(long *, u64 vec, u64 destCpu, u64 isLapic, u64, u64, u64) -> u64;
    	static auto syscallAllocGsi(long *ret, u64 port, u64 destCpu, u64 isLapic, u64, u64, u64) -> u64;
    	static auto syscallFreeGsi(long *, u64 gsi, u64 destCpu, u64 isLapic, u64, u64, u64) -> u64;
    	static auto syscallLockToCore(long *, u64 cpuId, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallGetCpuIDs(long *, u64 cpuIdOutArray, u64 cpuCount, u64, u64, u64, u64) -> u64;
    	static auto syscallAllocPhysPage(long *ret, u64, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallFreePhysPage(long *, u64 pageAddr, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallGetAffinity(long *, u64 tidPid, u64 cpuSetSize, u64 mask, u64, u64, u64) -> u64;
    	static auto syscallSetAffinity(long *, u64 tidPid, u64 cpuSetSize, u64 mask, u64, u64, u64) -> u64;
    	static auto syscallRegisterVfs(long *, u64 port, u64, u64, u64, u64, u64) -> u64;
    	static auto syscallRegisterEventHandler(long *, u64 port, u64 eventMask, u64, u64, u64, u64) -> u64;
    	static auto syscallVfsRequest(long *ret, u64 requestType, u64 request, u64 requestLength, u64 reply, u64 replyLength, u64) -> u64;

    	static void notifyThreadKilled(u64 pid, u64 tid);
    	static void notifyProcessKilled(u64 pid);

    private:
    	static void setGsBase(u64 gsBase);
    	static void setFsBase(u64 fsBase);
    	static auto getGsBase() -> u64;
    	static auto getFsBase() -> u64;

    	static auto userIrqHandler(u64 *ctx) -> u32;

    	static auto portWatchdog(u64 *) -> u32;

    public:
    	static LinkedList<IrqRegistration> irqRegistrations;
    	static LinkedList<KernelEventRegistration> eventRegistrations;
    	static u64 vfsPort;

		static SyscallFun horizonSyscalls[horizonSyscallAmount];
    	static SyscallFun linuxSyscalls[linuxSyscallAmount];
    };

    extern "C" void syscallHandler();
}

#endif
