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

    namespace {
        LinkedList<PortEntry> *portList {};

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

    PortEntry::~PortEntry() {
		this->waiters.clear(false);
		this->messages.clear(true);
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

        if (hdr->length > 0 && hdr->buffer == nullptr) {
            return EINVAL;
        }

        const bool prevIF = portLock.lock();

        PortEntry *entry = findPortUnlocked(port);

        if (entry == nullptr) {
            portLock.unlock(prevIF);

            return ENOENT;
        }

        const bool portPrevIF = entry->lock.lock();

        portLock.unlock(prevIF);

    	auto *message = new PortMessage();

    	if (message == nullptr) {
    		entry->lock.unlock(portPrevIF);

    		return ENOMEM;
    	}

        if (hdr->length > 0) {
            message->buffer = new u8[hdr->length];

            if (message->buffer == nullptr) {
                entry->lock.unlock(portPrevIF);
                delete message;

                return ENOMEM;
            }

            memcpy(message->buffer, hdr->buffer, hdr->length);
        }

        message->length = hdr->length;
    	message->sourcePort = sendPort;

        entry->messages.addEnd(message);

        Thread *waiter = nullptr;

        if (const auto *waiterEntry = entry->waiters.removeFirstEntry(); waiterEntry != nullptr) {
            waiter = waiterEntry->value;
            delete waiterEntry;
        }

        entry->lock.unlock(portPrevIF);

    	if (waiter != nullptr) {
    		const bool shouldWake = waiter->getState() == ThreadState::BLOCKED;
            waiter->setWaitingPort(0);

    		if (shouldWake) {
                CommonMain::getInstance()->getScheduler()->unblockThread(waiter->getId(), true);
    		}
        }

        hdr->retLength = static_cast<ssize>(message->length);

        return 0;
    }

    u64 PortMessaging::recvMessage(const u64 port, MessageHeader *hdr) {
        if (hdr == nullptr) {
            return EINVAL;
        }

        if (hdr->length > 0 && hdr->buffer == nullptr) {
            return EINVAL;
        }

        for (;;) {
            const bool prevIF = portLock.lock();

            PortEntry *entry = findPortUnlocked(port);

            if (entry == nullptr) {
                portLock.unlock(prevIF);

                return ENOENT;
            }

            const bool portPrevIF = entry->lock.lock();

            portLock.unlock(prevIF);

        	if (const auto *messageEntry = entry->messages.getFirst(); messageEntry != nullptr) {
				const auto *message = messageEntry->value;
        		const usize messageLength = message->length;

        		if (hdr->length < messageLength) {
        			entry->lock.unlock(portPrevIF);

        			return EMSGSIZE;
        		}

        		messageEntry = entry->messages.removeFirstEntry();

        		if (messageEntry == nullptr) {
        			entry->lock.unlock(portPrevIF);

        			return ENOENT;
        		}

        		message = messageEntry->value;
        		delete messageEntry;

        		if (messageLength > 0) {
        			memcpy(hdr->buffer, message->buffer, messageLength);
        		}

        		hdr->port = port;
        		hdr->srcPort = message->sourcePort;
        		hdr->retLength = static_cast<ssize>(messageLength);

        		entry->lock.unlock(portPrevIF);
        		delete message;

        		return 0;
        	}

            Thread *currThread = Scheduler::getCurrentThread();

            if (currThread == nullptr) {
                entry->lock.unlock(portPrevIF);

                return EFAULT;
            }

            if (!entry->waiters.contains(currThread)) {
                currThread->setWaitingPort(port);
                entry->waiters.addEnd(currThread);
            }

            entry->lock.unlock(portPrevIF);

            // Temporarily enable interrupts before blocking to allow the APIC timer to fire.
            // This is crucial for the scheduler to unblock this thread or schedule others.
            Asm::sti(); // Enable interrupts

            CommonMain::getInstance()->getScheduler()->blockThread(currThread->getId());

            // Disable interrupts again after returning from blockThread,
            // to maintain the syscall's disabled-interrupt context.
            Asm::cli(); // Disable interrupts
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

            currPort.waiters.remove(thread, false);

            currPort.lock.unlock(portPrevIF);
        }

        portLock.unlock(prevIF);
    }
}
