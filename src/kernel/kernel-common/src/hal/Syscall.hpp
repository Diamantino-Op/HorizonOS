#ifndef KERNEL_COMMON_SYSCALL_HPP
#define KERNEL_COMMON_SYSCALL_HPP

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

	constexpr u64 linuxSyscallAmount = 309;
	constexpr u64 horizonSyscallAmount = 32;

	struct MessageHeader {
		u64 port;               /* target port (for send) or port that received the message */
		u64 *buffer;            /* pointer to message buffer in user space */
		usize length;           /* buffer length */
		int flags;              /* message flags (e.g. MSG_DONTWAIT) */
		ssize retLength;        /* kernel-filled: number of bytes sent/received (or negative error) */
		u64 srcPort;            /* set by kernel on receive: source port */
		void *control;          /* optional ancillary/control data pointer */
		usize controlLen;       /* ancillary data length */
		u64 timeoutNs;		    /* optional timeout in nanoseconds (0 = wait indefinitely) */
	};

    class SyscallManager {
    public:
        static void init();
    	static void initArch();

    	// HorizonOS / Linux syscalls
		static u64 syscallPrint(long *, u64 message, u64, u64, u64, u64, u64);
    	static u64 syscallMMap(long *ret, u64 hint, u64 size, u64 prot, u64 flags, u64 fd, u64 offset);
    	static u64 syscallMUnmap(long *, u64 addr, u64 size, u64, u64, u64, u64);
    	static u64 syscallGetTID(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallArchCtl(long *ret, u64 operation, u64 pointer, u64, u64, u64, u64);
    	static u64 syscallExit(long *, u64 staus, u64, u64, u64, u64, u64);
    	static u64 syscallClockGet(long *ret, u64 clock, u64 ts, u64, u64, u64, u64);
    	static u64 syscallSysInfo(long *ret, u64 info, u64, u64, u64, u64, u64);
    	static u64 syscallGetCpu(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallKillThread(long *ret, u64 pid, u64 tid, u64 sig, u64, u64, u64);
    	static u64 syscallPause(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallThreadExit(long *, u64, u64, u64, u64, u64, u64);
    	static u64 syscallNewThread(long *ret, u64 entryFun, u64 stack, u64, u64, u64, u64);
    	static u64 syscallSendMsg(long *ret, u64 port, u64 msgHdr, u64, u64, u64, u64);
    	static u64 syscallRecvMsg(long *ret, u64 port, u64 msgHdr, u64, u64, u64, u64);
    	static u64 syscallRegisterPort(long *ret, u64 port, u64, u64, u64, u64, u64);
    	static u64 syscallIsThreadAlive(long *ret, u64 tid, u64, u64, u64, u64, u64);
    	static u64 syscallFutex(long *ret, u64 pointer, u64 type, u64 expected, u64 time, u64, u64);
        static u64 syscallSigreturn(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallSigaction(long *ret, u64 sig, u64 action, u64 oldAction, u64, u64, u64);
    	static u64 syscallMProtect(long *ret, u64 pointer, u64 size, u64 prot, u64, u64, u64);
    	static u64 syscallNanoSleep(long *ret, u64 secs, u64 nanos, u64, u64, u64, u64);
    	static u64 syscallIsaTTY(long *ret, u64 fd, u64, u64, u64, u64, u64);
    	static u64 syscallIoPerm(long *ret, u64 from, u64 num, u64 state, u64, u64, u64);
    	static u64 syscallIoPl(long *ret, u64 level, u64, u64, u64, u64, u64);
    	static u64 syscallKill(long *ret, u64 pid, u64 signal, u64, u64, u64, u64);
    	static u64 syscallGetPID(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallMMapPhys(long *ret, u64 physAddr, u64 len, u64, u64, u64, u64);
    	static u64 syscallGetRsdp(long *ret, u64, u64, u64, u64, u64, u64);
    	static u64 syscallInstallIRQHandler(long *ret, u64 irq, u64 handler, u64 ctx, u64, u64, u64);
    	static u64 syscallUninstallIRQHandler(long *ret, u64 handler, u64 handle, u64, u64, u64, u64);
    	static u64 syscallGetIRQMode(long *ret, u64, u64, u64, u64, u64, u64);

    private:
    	static void setGsBase(u64 gsBase);
    	static void setFsBase(u64 fsBase);
    	static u64 getGsBase();
    	static u64 getFsBase();

    public:
		static SyscallFun horizonSyscalls[horizonSyscallAmount];
    	static SyscallFun linuxSyscalls[linuxSyscallAmount];
    };

    extern "C" void syscallHandler();
}

#endif