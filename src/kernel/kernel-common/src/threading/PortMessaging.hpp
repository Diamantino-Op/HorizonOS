#ifndef KERNEL_COMMON_PORTMESSAGING_HPP
#define KERNEL_COMMON_PORTMESSAGING_HPP

#include "hal/Syscall.hpp"
#include "Scheduler.hpp"

namespace kernel::common::threading {
    using namespace hal;

	struct PortMessage {
		u64 sourcePort {};
		u64 type {};
		usize length {};
		u8 *buffer {};

		~PortMessage();
	};

	struct PortWaiter {
		Thread *thread {};
		u64 *blackListTypes {};
		usize blackListCount {};
		u64 *whiteListTypes {};
		usize whiteListCount {};

		~PortWaiter();
	};

	struct PortEntry {
		u64 port {};
		LinkedList<PortMessage> messages {};
		LinkedList<PortWaiter> waiters {};
		TicketSpinLock lock {};

		~PortEntry();
	};

    class PortMessaging {
    public:
    	static u64 getNewPort();
        static u64 registerPort(u64 port);
        static u64 sendMessage(u64 sendPort, u64 port, MessageHeader *hdr);
        static u64 recvMessage(u64 port, MessageHeader *hdr, const MessageFilterOptions *options);

        static void removeThread(Thread *thread);

    	static void debugDump();

    private:
        static PortEntry *findPortUnlocked(u64 port);
        static PortEntry *createPortUnlocked(u64 port);

        static TicketSpinLock portLock;
    	static u64 currUsedPort;
    };
}

#endif


