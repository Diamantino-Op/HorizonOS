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
    		auto *waiter = waiterNode->value;

    		if (waiter != nullptr && waiter->accepts(messageType)) {
    			// Return TID only — do NOT remove the waiter here.
    			// The waiter thread will remove itself when it dequeues the message.
    			return waiter->thread ? waiter->thread->getId() : 0;
    		}

    		waiterNode = waiterNode->next;
    	}

    	return 0;
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

    u64 PortMessaging::sendMessage(const u64 sendPort, const u64 port, MessageHeader *const hdr) {
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
        const bool prevPortIF = portLock.lock();

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
            CommonMain::getInstance()->getScheduler()->unblockThread(tidToWake, /*top=*/true, /*useLock=*/false);
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
            	const usize msgLen     = msg->length;
            	const u64   msgSrcPort = msg->sourcePort;
            	const u64   msgType    = msg->type;

            	if (!entry->messages.removeEntry(msgNode)) {
            		entry->lock.unlock(prevPortIF);

            		return ENOENT;
            	}

            	// Remove this thread's waiter entry from the port (it was registered
            	// when we first blocked; now that we have the message, clean it up).
	            {
                	auto *w = entry->waiters.getFirst();

                	while (w != nullptr) {
                		auto *next = w->next;

                		if (w->value != nullptr && w->value->thread == Scheduler::getCurrentThread()) {
                			const PortWaiter *dw = w->value;

                			entry->waiters.removeEntry(w);

                			delete dw;
                			delete w;

                			break;
                		}

                		w = next;
                	}
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
	        // Step 4b: no matching message.
	        // We must hold BOTH entry->lock AND schedLock while we:
	        //   1. Register the waiter in the port's waiter list.
	        //   2. Mark the thread BLOCKED.
	        //   3. Remove it from the run queue.
	        // This way sendMessage, which also acquires entry->lock before
	        // calling wakeOneWaiter, cannot sneak in between steps 1 and 2
	        // and fire an unblockThread on a thread that isn't sleeping yet.
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

	        auto *scheduler = CommonMain::getInstance()->getScheduler();

	        // Acquire schedLock while still holding entry->lock (both NoCli —
	        // IF is already disabled from portLock.lock() at the top of the loop).
	        // Locking order: portLock → entry->lock → schedLock.
	        // sendMessage order: portLock → entry->lock  (never touches schedLock).
	        // So no deadlock is possible.
	        scheduler->getSchedLock()->lockNoCli();

	        // --- With both locks held, check for a duplicate waiter ---
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
	            waiter = new PortWaiter();

	            if (waiter == nullptr) {
	                scheduler->getSchedLock()->unlockNoSti();
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

	                        scheduler->getSchedLock()->unlockNoSti();
	                        entry->lock.unlock(prevPortIF);

	                        return ENOMEM;
	                    }

	                    memcpy(waiter->blackListTypes, options->blackListTypes, options->blackListCount * sizeof(u64));
	                }

	                if (options->whiteListTypes != nullptr && options->whiteListCount > 0) {
	                    waiter->whiteListTypes = new u64[options->whiteListCount];

	                    if (waiter->whiteListTypes == nullptr) {
	                        delete waiter;

	                        scheduler->getSchedLock()->unlockNoSti();
	                        entry->lock.unlock(prevPortIF);

	                        return ENOMEM;
	                    }

	                    memcpy(waiter->whiteListTypes, options->whiteListTypes, options->whiteListCount * sizeof(u64));
	                }
	            }

	            // Commit waiter to the port BEFORE marking thread as blocked.
	            // sendMessage holds entry->lock when it calls wakeOneWaiter, so
	            // it cannot see this waiter until we release entry->lock below —
	            // by which point the thread is already BLOCKED and in blockedList.
	            entry->waiters.addEnd(waiter);
	        }

	        // Mark the thread BLOCKED under schedLock so the scheduler
	        // won't re-queue it and sendMessage's unblockThread sees a
	        // consistent state.
	        currThread->setState(ThreadState::BLOCKED);
	        currThread->setWaitingPort(port);

	        const bool isCurrentThread = Scheduler::getCurrentExecutionNode()->getCurrentThread() == currThread;

	        scheduler->removeThread(currThread);

	        if (!isCurrentThread) {
	            scheduler->blockedThreadList.addStart(currThread);
	        }

	        // Release both locks together. entry->lock first (NoSti — keep IF
	        // disabled), then schedLock with the original prevPortIF to finally
	        // restore the pre-syscall interrupt state.
	        entry->lock.unlockNoSti();
	        scheduler->getSchedLock()->unlock(prevPortIF);  // restores IF

	        if (isCurrentThread) {
	            ExecutionNode::reSchedule();
	        }
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
