#include "PortMessaging.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::threading {
    using namespace kernel::common::hal;
    using namespace kernel::common::memory;

    // =========================================================================
    // Static member definitions
    // =========================================================================

    TicketSpinLock PortMessaging::portLock   {};
    u64            PortMessaging::currUsedPort = 2; // 0 = invalid, 1 = kernel-reserved

    // =========================================================================
    // Module-internal state
    // =========================================================================

    namespace {
        // Global port list.  Allocated on first use; never freed.
        LinkedList<PortEntry> *portList {};

        LinkedList<PortEntry> &getPortList() {
            if (portList == nullptr) {
                portList = new LinkedList<PortEntry>();
            }
            return *portList;
        }
    } // anonymous namespace

    // =========================================================================
    // PortMessage
    // =========================================================================

    PortMessage::~PortMessage() {
        delete[] buffer;
    }

    // =========================================================================
    // PortWaiter
    // =========================================================================

    PortWaiter::~PortWaiter() {
        delete[] blackListTypes;
        delete[] whiteListTypes;
    }

    bool PortWaiter::accepts(const u64 messageType) const {
        // 1. Black-list check — explicitly blocked types are rejected first.
        if (blackListTypes != nullptr && blackListCount > 0) {
            for (usize i = 0; i < blackListCount; ++i) {
                if (messageType == blackListTypes[i]) {
                    return false;
                }
            }
        }

        // 2. White-list check — if a white-list is defined, the type must be in it.
        if (whiteListTypes != nullptr && whiteListCount > 0) {
            for (usize i = 0; i < whiteListCount; ++i) {
                if (messageType == whiteListTypes[i]) {
                    return true;
                }
            }
            return false; // type not in white-list
        }

        return true; // no white-list filter → accept everything not black-listed
    }

    // =========================================================================
    // PortEntry
    // =========================================================================

    PortEntry::~PortEntry() {
        waiters.clear(true);
        messages.clear(true);
    }

    // =========================================================================
    // Helpers — must be called with portLock held
    // =========================================================================

    PortEntry *PortMessaging::findPortUnlocked(const u64 port) {
        for (auto &entry : getPortList()) {
            if (entry.port == port) {
                return &entry;
            }
        }
        return nullptr;
    }

    PortEntry *PortMessaging::createPortUnlocked(const u64 port) {
        auto *entry = new PortEntry();
        if (entry == nullptr) {
            return nullptr;
        }
        entry->port = port;
        getPortList().addEnd(entry);
        return entry;
    }

    // =========================================================================
    // Helper — wake the first matching waiter.
    // Called with entry->lock held; portLock must NOT be held.
    // Returns the TID of the thread unblocked, or 0 if none.
    // =========================================================================

    u16 PortMessaging::wakeOneWaiter(PortEntry *entry, const u64 messageType) {
        auto *waiterNode = entry->waiters.getFirst();

        while (waiterNode != nullptr) {
            auto *next    = waiterNode->next;
            auto *waiter  = waiterNode->value;

            if (waiter != nullptr && waiter->accepts(messageType)) {
                const u16 tid = waiter->thread ? waiter->thread->getId() : 0;

                // Remove the waiter node before releasing any lock —
                // the waiter pointer must not be accessed after this.
                if (entry->waiters.removeEntry(waiterNode)) {
                    delete waiter;
                    delete waiterNode;
                }

                return tid;
            }

            waiterNode = next;
        }

        return 0; // no suitable waiter found
    }

    // =========================================================================
    // Public API
    // =========================================================================

    u64 PortMessaging::getNewPort() {
        const bool prevIF = portLock.lock();

        if (currUsedPort == 0) {
            // Wrapped around — u64 exhaustion is theoretically impossible but
            // handle it defensively.
            portLock.unlock(prevIF);
            return 0;
        }

        const u64 next = currUsedPort++;
        portLock.unlock(prevIF);
        return next;
    }

    u64 PortMessaging::registerPort(const u64 port) {
        if (port == 0) {
            return EINVAL;
        }

        const bool prevIF = portLock.lock();

        if (findPortUnlocked(port) != nullptr) {
            portLock.unlock(prevIF);
            return EEXIST;
        }

        if (createPortUnlocked(port) == nullptr) {
            portLock.unlock(prevIF);
            return ENOMEM;
        }

        portLock.unlock(prevIF);
        return 0;
    }

    // =========================================================================
    // sendMessage
    //
    // Locking protocol
    // ----------------
    //  1. Acquire portLock (coarse).
    //  2. Locate the PortEntry.
    //  3. Acquire entry->lock (fine) — saved IF state is entry-specific.
    //  4. Release portLock while still holding entry->lock.
    //  5. Enqueue the message.
    //  6. Find and remove a suitable waiter; save TID only (no pointer).
    //  7. Release entry->lock.
    //  8. Unblock the saved TID outside all locks.
    // =========================================================================

    u64 PortMessaging::sendMessage(const u64 sendPort, const u64 port,
                                   MessageHeader *const hdr) {
        if (hdr == nullptr) {
            return EINVAL;
        }

        if (hdr->port != 0 && hdr->port != port) {
            return EINVAL;
        }

        if (hdr->length > 0 && hdr->buffer == nullptr) {
            return EINVAL;
        }

        // --- Locate port (coarse lock) ---
        bool prevPortIF = portLock.lock();

        PortEntry *entry = findPortUnlocked(port);
        if (entry == nullptr) {
            portLock.unlock(prevPortIF);
            return ENOENT;
        }

        // Promote to per-entry lock, then drop the coarse lock.
        bool prevEntryIF = entry->lock.lock();
        portLock.unlock(prevPortIF); // safe: we hold entry->lock, entry won't disappear

        // --- Allocate and fill message ---
        auto *msg = new PortMessage();
        if (msg == nullptr) {
            entry->lock.unlock(prevEntryIF);
            return ENOMEM;
        }

        if (hdr->length > 0) {
            msg->buffer = new u8[hdr->length];
            if (msg->buffer == nullptr) {
                delete msg;
                entry->lock.unlock(prevEntryIF);
                return ENOMEM;
            }
            memcpy(msg->buffer, hdr->buffer, hdr->length);
        }

        msg->length     = hdr->length;
        msg->sourcePort = sendPort;
        msg->type       = hdr->type;

        hdr->retLength = static_cast<ssize>(msg->length);

        entry->messages.addEnd(msg);

        // --- Wake a waiter (if any) ---
        // wakeOneWaiter removes the waiter node and returns the TID.
        // We must not dereference any waiter pointer after this call.
        const u16 tidToWake = wakeOneWaiter(entry, hdr->type);

        entry->lock.unlock(prevEntryIF);

        // Unblock outside all locks so the scheduler can run freely.
        if (tidToWake != 0) {
            CommonMain::getInstance()->getScheduler()
                ->unblockThread(tidToWake, /*top=*/true, /*useLock=*/false);
        }

        return 0;
    }

    // =========================================================================
    // recvMessage
    //
    // Blocking receive loop.  The thread loops until a matching message is
    // dequeued or (if timeoutNs > 0) the deadline expires.
    //
    // Locking protocol inside the loop
    // ----------------------------------
    //  1. Acquire portLock.
    //  2. Locate entry; acquire entry->lock via lockNoCli().
    //  3. Release portLock via unlockNoSti() — interrupts still disabled.
    //  4. Walk message queue.
    //     a. Found matching message → copy out, delete, unlock, return.
    //     b. No match → register waiter, block under schedLock, reschedule.
    //  5. On wake-up, loop back to step 1.
    // =========================================================================

    u64 PortMessaging::recvMessage(const u64 port, MessageHeader *const hdr, const MessageFilterOptions *const options) {
        if (hdr == nullptr) {
            return EINVAL;
        }

        if (hdr->length > 0 && hdr->buffer == nullptr) {
            return EINVAL;
        }

        // Compute optional deadline.
        const u64 deadlineNs = (hdr->timeoutNs > 0) ? (CommonMain::getInstance()->getClocks()->getMainClock()->getNs() + hdr->timeoutNs) : 0;

        for (;;) {
            // -----------------------------------------------------------------
            // Step 1-3: lock hierarchy
            // -----------------------------------------------------------------
            bool prevPortIF = portLock.lock();

            PortEntry *entry = findPortUnlocked(port);

            if (entry == nullptr) {
                portLock.unlock(prevPortIF);

                return ENOENT;
            }

            entry->lock.lockNoCli(); // acquires entry lock, keeps IF disabled
            portLock.unlockNoSti();                     // drops port lock, IF stays disabled

            // -----------------------------------------------------------------
            // Step 4a: walk the message queue for a matching message
            // -----------------------------------------------------------------
            auto *msgNode = entry->messages.getFirst();

            while (msgNode != nullptr) {
                const PortMessage *msg = msgNode->value;

                // --- Apply filter ---
                bool filtered = false;

                if (options != nullptr) {
                    if (options->blackListTypes != nullptr && options->blackListCount > 0) {
                        for (usize i = 0; i < options->blackListCount; ++i) {
                            if (msg->type == options->blackListTypes[i]) {
                                filtered = true;

                                break;
                            }
                        }
                    }

                    if (!filtered && options->whiteListTypes != nullptr && options->whiteListCount > 0) {
                        filtered = true;

                        for (usize i = 0; i < options->whiteListCount; ++i) {
                            if (msg->type == options->whiteListTypes[i]) {
                                filtered = false;

                                break;
                            }
                        }
                    }
                }

                if (filtered) {
                    msgNode = msgNode->next;

                    continue;
                }

                // Message passes the filter — check buffer capacity.
                if (hdr->length < msg->length) {
                    entry->lock.unlock(prevPortIF);

                    return EMSGSIZE;
                }

                // Snapshot all fields before we delete the node.
                const usize  msgLen    = msg->length;
                const u64    msgSrcPort = msg->sourcePort;
                const u64    msgType   = msg->type;

                if (!entry->messages.removeEntry(msgNode)) {
                    entry->lock.unlock(prevPortIF);

                    return ENOENT; // should never happen
                }

                if (msgLen > 0) {
                    memcpy(hdr->buffer, msg->buffer, msgLen);
                }

                hdr->port      = port;
                hdr->srcPort   = msgSrcPort;
                hdr->type      = msgType;
                hdr->retLength = static_cast<ssize>(msgLen);

                delete msg;
                delete msgNode;

                entry->lock.unlock(prevPortIF);

                return 0;
            }

            // -----------------------------------------------------------------
            // Step 4b: no matching message — check timeout before blocking
            // -----------------------------------------------------------------
            if (deadlineNs != 0) {
                const u64 nowNs = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

                if (nowNs >= deadlineNs) {
                    entry->lock.unlock(prevPortIF);

                    return ETIMEDOUT;
                }
            }

            Thread *currThread = Scheduler::getCurrentThread();

            if (currThread == nullptr) {
                entry->lock.unlock(prevPortIF);

                return EFAULT;
            }

            // Check for a duplicate waiter entry (same thread already registered).
            bool alreadyWaiting = false;
            {
                auto *w = entry->waiters.getFirst();

                while (w != nullptr) {
                    if (w->value != nullptr && w->value->thread == currThread) {
                        alreadyWaiting = true;

                        break;
                    }

                    w = w->next;
                }
            }

            PortWaiter *waiter = nullptr;

            if (!alreadyWaiting) {
                // Deep-copy filter options into the waiter.
                waiter = new PortWaiter();

                if (waiter == nullptr) {
                    entry->lock.unlock(prevPortIF);

                    return ENOMEM;
                }

                waiter->thread = currThread;

                if (options != nullptr) {
                    waiter->blackListCount = options->blackListCount;
                    waiter->whiteListCount = options->whiteListCount;

                    if (options->blackListTypes != nullptr && options->blackListCount > 0) {
                        waiter->blackListTypes = new u64[options->blackListCount];

                        if (waiter->blackListTypes == nullptr) {
                            delete waiter;

                            entry->lock.unlock(prevPortIF);

                            return ENOMEM;
                        }

                        memcpy(waiter->blackListTypes, options->blackListTypes, options->blackListCount * sizeof(u64));
                    }

                    if (options->whiteListTypes != nullptr && options->whiteListCount > 0) {
                        waiter->whiteListTypes = new u64[options->whiteListCount];

                        if (waiter->whiteListTypes == nullptr) {
                            // blackListTypes is freed by ~PortWaiter via delete waiter
                            delete waiter;

                            entry->lock.unlock(prevPortIF);

                            return ENOMEM;
                        }

                        memcpy(waiter->whiteListTypes, options->whiteListTypes, options->whiteListCount * sizeof(u64));
                    }
                }
            }

            // -----------------------------------------------------------------
            // Acquire schedLock (no CLI needed — IF already disabled).
            // Set thread to BLOCKED under both entry->lock and schedLock so
            // sendMessage cannot miss the wakeup.
            // -----------------------------------------------------------------
            auto *scheduler = CommonMain::getInstance()->getScheduler();
            scheduler->getSchedLock()->lockNoCli();

            currThread->setState(ThreadState::BLOCKED);
            currThread->setWaitingPort(port);

            scheduler->queues[currThread->getParent()->getPriority()].remove(currThread, false);

            const bool isCurrentThread = Scheduler::getCurrentExecutionNode()->getCurrentThread()->value == currThread;

            if (!isCurrentThread) {
                scheduler->blockedThreadList.addStart(currThread);
            }

            // Check for a pending wakeup that arrived between our message scan
            // and acquiring schedLock (covers both current and non-current paths).
            if (currThread->getPendingWakeup()) {
                if (!alreadyWaiting) {
                    delete waiter;
                }

                currThread->setPendingWakeup(false);
                currThread->setWaitingPort(0);
                currThread->setState(ThreadState::RUNNING);

                if (!isCurrentThread) {
                    scheduler->blockedThreadList.remove(currThread, false);
                    scheduler->queues[currThread->getParent()->getPriority()].addStart(currThread);
                }

                scheduler->getSchedLock()->unlock(prevPortIF);
                // Loop back to retry dequeuing.

                continue;
            }

            // Commit the waiter to the list, drop entry lock, then reschedule.
            if (!alreadyWaiting) {
                entry->waiters.addEnd(waiter);
            }

            entry->lock.unlockNoSti(); // entry lock released; IF still disabled

            scheduler->getSchedLock()->unlock(prevPortIF); // re-enables IF

            if (isCurrentThread) {
                ExecutionNode::reSchedule(); // yields; returns when unblocked
            }
            // Non-current thread: will be rescheduled on its own CPU naturally.

            // Loop back to retry.
        }
    }

    // =========================================================================
    // removeThread — purge all waiters belonging to `thread`
    // =========================================================================

    void PortMessaging::removeThread(Thread *const thread) {
        if (thread == nullptr) {
            return;
        }

        const bool prevPortIF = portLock.lock();

        thread->setWaitingPort(0);

        for (auto &currPort : getPortList()) {
            const bool portPrevIF = currPort.lock.lock();

            auto *waiterNode = currPort.waiters.getFirst();

            while (waiterNode != nullptr) {
                auto *next   = waiterNode->next;
                auto *waiter = waiterNode->value;

                if (waiter != nullptr && waiter->thread == thread) {
                    if (currPort.waiters.removeEntry(waiterNode)) {
                        delete waiter;
                        delete waiterNode;
                    }
                }

                waiterNode = next;
            }

            currPort.lock.unlock(portPrevIF);
        }

        portLock.unlock(prevPortIF);
    }

    // =========================================================================
    // debugDump
    // =========================================================================

    void PortMessaging::debugDump() {
        auto *term = CommonMain::getTerminal();

        term->warnNoLock("  === PORT MESSAGING DUMP ===", "SchedDump");

        if (portList == nullptr) {
            term->warnNoLock("    portList is null", "SchedDump");
            return;
        }

        for (auto &entry : *portList) {
            term->warnNoLock("  Port=%lu  messages=%lu  waiters=%lu",
                             "SchedDump",
                             entry.port,
                             entry.messages.getSize(),
                             entry.waiters.getSize());

            for (const auto &msg : entry.messages) {
                term->warnNoLock("    Msg: type=%lu  srcPort=%lu  length=%lu",
                                 "SchedDump", msg.type, msg.sourcePort, msg.length);
            }

            for (const auto &w : entry.waiters) {
                if (w.thread == nullptr) continue;
                term->warnNoLock("    Waiter: TID=%u  white=%lu  black=%lu",
                                 "SchedDump",
                                 w.thread->getId(),
                                 w.whiteListCount,
                                 w.blackListCount);

                for (usize i = 0; i < w.whiteListCount; ++i) {
                    term->warnNoLock("      whitelist[%lu] = %lu",
                                     "SchedDump", i, w.whiteListTypes[i]);
                }

                for (usize i = 0; i < w.blackListCount; ++i) {
                    term->warnNoLock("      blacklist[%lu] = %lu",
                                     "SchedDump", i, w.blackListTypes[i]);
                }
            }
        }
    }
}