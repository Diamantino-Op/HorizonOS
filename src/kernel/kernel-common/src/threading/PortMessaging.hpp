#ifndef KERNEL_COMMON_PORTMESSAGING_HPP
#define KERNEL_COMMON_PORTMESSAGING_HPP

#include "hal/Syscall.hpp"
#include "Scheduler.hpp"

namespace kernel::common::threading {
    using namespace hal;

    // -------------------------------------------------------------------------
    // PortMessage — one queued message on a port.
    // The kernel owns the buffer; it is freed in the destructor.
    // -------------------------------------------------------------------------
    struct PortMessage {
        u64   sourcePort {};
        u64   type       {};
        usize length     {};
        u8   *buffer     {};

	PortMessage() = default;
        ~PortMessage();

        // Non-copyable, non-movable — always heap-allocated via new.
        PortMessage(const PortMessage &) = delete;
        PortMessage &operator=(const PortMessage &) = delete;
    };

    // -------------------------------------------------------------------------
    // PortWaiter — a thread blocked waiting for a message on a port.
    // Filter arrays are owned by this struct (deep-copied from user options).
    // -------------------------------------------------------------------------
    struct PortWaiter {
        Thread *thread        {};
	u64     generation    {};
        u64    *blackListTypes {};
        usize   blackListCount {};
        u64    *whiteListTypes {};
        usize   whiteListCount {};

	PortWaiter() = default;
        ~PortWaiter();

        // Returns true if this waiter accepts a message of the given type.
        [[nodiscard]] bool accepts(u64 messageType) const;

        PortWaiter(const PortWaiter &) = delete;
        PortWaiter &operator=(const PortWaiter &) = delete;
    };

    // -------------------------------------------------------------------------
    // PortEntry — a registered port with its message queue and waiter list.
    // -------------------------------------------------------------------------
    struct PortEntry {
        u64                     port     {};
        LinkedList<PortMessage> messages {};
        LinkedList<PortWaiter>  waiters  {};
        TicketSpinLock          lock     {};

	PortEntry() = default;
        ~PortEntry();

        PortEntry(const PortEntry &) = delete;
        PortEntry &operator=(const PortEntry &) = delete;
    };

    // -------------------------------------------------------------------------
    // PortMessaging — the kernel IPC subsystem.
    //
    //  Port 0   : reserved / invalid.
    //  Port 1   : reserved for kernel-internal use.
    //  Port 2+  : dynamically allocated via getNewPort().
    //
    //  Thread safety
    //  -------------
    //  portLock  : coarse lock protecting the global port list and currUsedPort.
    //  entry->lock : per-port lock protecting message queue and waiter list.
    //
    //  Locking order is always: portLock → entry->lock.
    //  Never acquire portLock while holding any entry->lock.
    // -------------------------------------------------------------------------
    class PortMessaging {
    public:
        // Allocate the next free port number (thread-safe).
        // Returns 0 if the ID space is exhausted (extremely unlikely).
        static u64 getNewPort();

        // Register a port so it can receive messages.
        // Returns 0 on success, errno on failure (EINVAL, EEXIST, ENOMEM).
        static u64 registerPort(u64 port);

        // Send a message to `port`.  `sendPort` identifies the sender.
        // `hdr->retLength` is filled with the number of bytes enqueued.
        // Returns 0 on success, errno on failure.
        static u64 sendMessage(u64 sendPort, u64 port, MessageHeader *hdr);

        // Receive a message from `port` into `hdr->buffer`.
        // Blocks the calling thread until a matching message arrives
        // (or until `hdr->timeoutNs` nanoseconds elapse, if non-zero).
        // Returns 0 on success, ETIMEDOUT on timeout, errno on failure.
        static u64 recvMessage(u64 port, MessageHeader *hdr, const MessageFilterOptions *options);

        // Remove all waiters belonging to `thread` from every port.
        // Must be called before the thread is destroyed.
        static void removeThread(Thread *thread);

        // Print a human-readable dump of all ports (for debugging).
        static void debugDump();

    private:
	struct WakeTarget {
		u16 tid {};
		u64 generation {};
	};

        // Must be called with portLock held.
        static PortEntry *findPortUnlocked(u64 port);
        static PortEntry *createPortUnlocked(u64 port);

        // Remove and return the first waiter that accepts `messageType`.
        // Must be called with entry->lock held but portLock released.
        // Returns the thread ID that should be unblocked (0 if none).
        static WakeTarget wakeOneWaiter(PortEntry *entry, u64 messageType);

        static TicketSpinLock portLock;
        static u64            currUsedPort;   // next port to hand out (starts at 2)
    };
}

#endif
