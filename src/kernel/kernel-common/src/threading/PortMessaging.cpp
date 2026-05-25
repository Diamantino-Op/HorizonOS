#include "PortMessaging.hpp"

#include "CommonMain.hpp"
#include "ErrNo.hpp"
#include "memory/MainMemory.hpp"
#include "utils/Asm.hpp" // Added for Asm::sti() and Asm::cli()

namespace kernel::common::threading {
    using namespace kernel::common::hal;
    using namespace kernel::common::memory;
    using namespace kernel::x86_64::utils; // Added for Asm::sti() and Asm::cli()

    TicketSpinLock PortMessaging::portLock {};
	u64 PortMessaging::currUsedPort = 2;

    namespace {
        LinkedList<PortEntry> *portList {};

        bool waiterAcceptsMessage(const PortWaiter *waiter, const u64 messageType) {
            if (waiter == nullptr) {
                return false;
            }

            if (waiter->blackListTypes != nullptr && waiter->blackListCount > 0) {
                for (usize i = 0; i < waiter->blackListCount; ++i) {
                    if (messageType == waiter->blackListTypes[i]) {
                        return false;
                    }
                }
            }

            if (waiter->whiteListTypes != nullptr && waiter->whiteListCount > 0) {
                for (usize i = 0; i < waiter->whiteListCount; ++i) {
                    if (messageType == waiter->whiteListTypes[i]) {
                        return true;
                    }
                }

                return false;
            }

            return true;
        }

        PortWaiter *createWaiter(Thread *thread, const MessageFilterOptions *options) {
            auto *waiter = new PortWaiter();

            if (waiter == nullptr) {
                return nullptr;
            }

            waiter->thread = thread;

            if (options != nullptr) {
                waiter->blackListCount = options->blackListCount;
                waiter->whiteListCount = options->whiteListCount;

                if (options->blackListTypes != nullptr && options->blackListCount > 0) {
                    waiter->blackListTypes = new u64[options->blackListCount];

                    if (waiter->blackListTypes == nullptr) {
                        delete waiter;

                        return nullptr;
                    }

                    memcpy(waiter->blackListTypes, options->blackListTypes, options->blackListCount * sizeof(u64));
                }

                if (options->whiteListTypes != nullptr && options->whiteListCount > 0) {
                    waiter->whiteListTypes = new u64[options->whiteListCount];

                    if (waiter->whiteListTypes == nullptr) {
                        delete waiter;

                        return nullptr;
                    }

                    memcpy(waiter->whiteListTypes, options->whiteListTypes, options->whiteListCount * sizeof(u64));
                }
            }

            return waiter;
        }

        LinkedList<PortEntry> &getPortListUnlocked() {
            if (portList == nullptr) {
                portList = new LinkedList<PortEntry>();
            }

            return *portList;
        }
    }

    PortMessage::~PortMessage() {
        delete[] this->buffer;
    }

    PortWaiter::~PortWaiter() {
		delete[] this->blackListTypes;
		delete[] this->whiteListTypes;
    }

    PortEntry::~PortEntry() {
		this->waiters.clear(true);
		this->messages.clear(true);
    }

	u64 PortMessaging::getNewPort() {
      const bool prevIF = portLock.lock();

      const u64 nextPort = currUsedPort++;

      portLock.unlock(prevIF);

      return nextPort;
    }

    PortEntry *PortMessaging::findPortUnlocked(const u64 port) {
        auto &ports = getPortListUnlocked();

        for (auto &currPort : ports) {
            if (currPort.port == port) {
                return &currPort;
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

        getPortListUnlocked().addEnd(entry);

        return entry;
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

    u64 PortMessaging::sendMessage(const u64 sendPort, const u64 port, MessageHeader *hdr) {
        if (hdr == nullptr) {
            return EINVAL;
        }

        if (hdr->port != 0 && hdr->port != port) {
            return EINVAL;
        }

        if (hdr->length > 0 and hdr->buffer == nullptr) {
            return EINVAL;
        }

        bool prevIf = portLock.lock();

        PortEntry *entry = findPortUnlocked(port);

        if (entry == nullptr) {
            portLock.unlock(prevIf);

            return ENOENT;
        }

        bool prevEntryIf = entry->lock.lock();

        portLock.unlock(prevIf);

    	auto *message = new PortMessage();

    	if (message == nullptr) {
    		entry->lock.unlock(prevEntryIf);

    		return ENOMEM;
    	}

        if (hdr->length > 0) {
            message->buffer = new u8[hdr->length];

            if (message->buffer == nullptr) {
                entry->lock.unlock(prevEntryIf);

                delete message;

                return ENOMEM;
            }

            memcpy(message->buffer, hdr->buffer, hdr->length);
        }

        message->length = hdr->length;
    	message->sourcePort = sendPort;
    	message->type = hdr->type;

    	hdr->retLength = static_cast<ssize>(message->length);

        entry->messages.addEnd(message);

        const PortWaiter *waiter = nullptr;

        auto *waiterEntry = entry->waiters.getFirst();

        while (waiterEntry != nullptr) {
            auto *nextWaiter = waiterEntry->next;
            auto *currWaiter = waiterEntry->value;

            if (waiterAcceptsMessage(currWaiter, hdr->type)) {
                if (entry->waiters.removeEntry(waiterEntry)) {
                    waiter = currWaiter;

                    break;
                }
            }

            waiterEntry = nextWaiter;
        }

    	// Save thread ID BEFORE releasing any lock or deleting anything
    	const u16 threadId = (waiter != nullptr) ? waiter->thread->getId() : 0;

    	// Now it's safe to delete and unlock
    	if (waiter != nullptr) {
    		delete waiter;
    		delete waiterEntry;
    	}

    	entry->lock.unlock(prevEntryIf);

    	// Unblock AFTER cleaning up, using the saved ID (no pointer dereference)
    	if (threadId != 0) {
    		CommonMain::getInstance()->getScheduler()->unblockThread(threadId, true);
    	}

        return 0;
    }

    u64 PortMessaging::recvMessage(const u64 port, MessageHeader *hdr, const MessageFilterOptions *options) {
        if (hdr == nullptr) {
	        return EINVAL;
	    }

	    if (hdr->length > 0 && hdr->buffer == nullptr) {
	        return EINVAL;
	    }

	    for (;;) {
		    bool prevIf = portLock.lock();

    		PortEntry *entry = findPortUnlocked(port);

    		if (entry == nullptr) {
    			portLock.unlock(prevIf);

    			return ENOENT;
    		}

    		bool prevEntryIf = entry->lock.lock();

    		portLock.unlock(prevIf);

    		// Walk the message queue to find the first message that passes the filter.
    		auto *messageEntry = entry->messages.getFirst();

    		while (messageEntry != nullptr) {
    			auto *message = messageEntry->value;

    			// --- Filter check ---
    			if (options != nullptr) {
    				bool filtered = false;

    				// Step 1: Blacklist — block if the type is in the blacklist.
    				if (options->blackListTypes != nullptr && options->blackListCount > 0) {
    					for (usize i = 0; i < options->blackListCount; ++i) {
    						if (message->type == options->blackListTypes[i]) {
    							filtered = true;

    							break;
    						}
    					}
    				}

    				// Step 2: Whitelist — if provided, block unless the type is in the whitelist.
    				// Applied after the blacklist, so a type blacklisted AND whitelisted is still blocked.
    				if (!filtered && options->whiteListTypes != nullptr && options->whiteListCount > 0) {
    					filtered = true; // Assume blocked unless found

    					for (usize i = 0; i < options->whiteListCount; ++i) {
    						if (message->type == options->whiteListTypes[i]) {
    							filtered = false;

    							break;
    						}
    					}
    				}

    				if (filtered) {
    					messageEntry = messageEntry->next;

    					continue;
    				}
    			}
    			// --- End filter check ---

    			// Found a matching message.
    			const usize messageLength = message->length;

    			if (hdr->length < messageLength) {
    				entry->lock.unlock(prevEntryIf);

    				return EMSGSIZE;
    			}

    			// Remove this specific entry from the queue.
    			if (not entry->messages.removeEntry(messageEntry)) {
    				entry->lock.unlock(prevEntryIf);

    				return ENOENT;
    			}

    			message = messageEntry->value;

    			if (messageLength > 0) {
    				memcpy(hdr->buffer, message->buffer, messageLength);
    			}

    			hdr->port      = port;
    			hdr->srcPort   = message->sourcePort;
    			hdr->type      = message->type;
    			hdr->retLength = static_cast<ssize>(messageLength);

    			entry->lock.unlock(prevEntryIf);

    			delete message;
    			delete messageEntry;

    			return 0;
    		}

    		// No matching message found — block and wait.
    		Thread *currThread = Scheduler::getCurrentThread();

    		if (currThread == nullptr) {
    			entry->lock.unlock(prevEntryIf);

    			return EFAULT;
    		}

            bool alreadyWaiting = false;
            const auto *waiterEntry = entry->waiters.getFirst();

            while (waiterEntry != nullptr) {
              if (waiterEntry->value != nullptr && waiterEntry->value->thread == currThread) {
                alreadyWaiting = true;

                break;
              }

              waiterEntry = waiterEntry->next;
            }

	    	currThread->setWaitingPort(port);

            if (!alreadyWaiting) {
              auto *waiter = createWaiter(currThread, options);

              if (waiter == nullptr) {
                entry->lock.unlock(prevEntryIf);

                return ENOMEM;
              }

              entry->waiters.addEnd(waiter);
            }

    		entry->lock.unlock(prevEntryIf);

    		//Asm::sti();
    		CommonMain::getInstance()->getScheduler()->blockThread(currThread->getId());
	    	//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 500ull * 1'000'000ull);
    		//Asm::cli();
	    }
    }

    void PortMessaging::removeThread(Thread *thread) {
        if (thread == nullptr) {
            return;
        }

        const bool prevIF = portLock.lock();
        thread->setWaitingPort(0);

        auto &ports = getPortListUnlocked();

        for (auto &currPort : ports) {
            const bool portPrevIF = currPort.lock.lock();

            auto *waiterEntry = currPort.waiters.getFirst();

            while (waiterEntry != nullptr) {
                auto *nextWaiter = waiterEntry->next;

                if (waiterEntry->value != nullptr && waiterEntry->value->thread == thread) {
                    if (currPort.waiters.removeEntry(waiterEntry)) {
                        delete waiterEntry->value;
                        delete waiterEntry;
                    }
                }

                waiterEntry = nextWaiter;
            }

            currPort.lock.unlock(portPrevIF);
        }

        portLock.unlock(prevIF);
    }
}
