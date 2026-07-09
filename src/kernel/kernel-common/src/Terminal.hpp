#ifndef KERNEL_COMMON_TERMINAL_HPP
#define KERNEL_COMMON_TERMINAL_HPP

#include "Types.hpp"

#include "limine.h"
#include "flanterm.h"

#include "SpinLock.hpp"
#include "LFQueue.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::common {
    using namespace threading;

	constexpr u64 maxMsgLength = 256;
	constexpr u64 maxMsgIDLength = 64;
	constexpr u64 maxMessages = 1024;
	constexpr u64 maxPersistentInfoLogs = 4096;

	enum MessageType {
		DEBUG,
		INFO,
		WARN,
		ERROR
	};

	struct TermMsg {
		MessageType type;
		char id[maxMsgIDLength];
		char msg[maxMsgLength];
	};

	struct KernelLogEntry {
		u64 sequence {};
		u64 timestampNs {};
		MessageType type {};
		char id[maxMsgIDLength] {};
		char msg[maxMsgLength] {};
	};

    class Terminal {
    public:
        Terminal() = default;

        explicit Terminal(const limine_framebuffer *framebuffer);

        bool lock();
        void unlock(bool prevIF);

    	static bool canPrint();

        static void putChar(int c, void *ctx);
        static void putCharE9(int c, void *ctx);
        static void putCharBoth(int c, void *ctx);
    	static void putCharCOM2(int c, void *ctx);

        void printf(bool autoSN, const char* format, ...);
        void printfE9(bool autoSN, const char* format, ...);
        void printfBoth(bool autoSN, const char* format, ...);
        void printfUAcpi(bool autoSN, const char* format, ...);
        void printInterruptFrame(u64 *framePtr);

    	void printfCOM2(bool autoSN, const char* format, ...);

        void info(const char *format, const char *id, ...);
        void debug(const char *format, const char *id, ...);
        void warn(const char *format, const char *id, ...);
        void warnNoLock(const char *format, const char *id, ...);
	        void error(const char *format, const char *id, ...);
	    	auto readInfoLog(u64 afterSequence, KernelLogEntry *out, usize maxEntries) -> usize;

        ExecutionNode *getCurrentCore();

    private:
    	void enqueueMessage(const TermMsg &message);
    	void appendPersistentInfoLog(const TermMsg &message);
    	void wakeThread();

        static flanterm_context *flantermCtx;

        TicketSpinLock spinLock;

    public:
    	LFQueue<TermMsg, maxMessages> msgQueue {};
    	KernelLogEntry persistentInfoLogs[maxPersistentInfoLogs] {};
    	TicketSpinLock persistentInfoLogLock;
    	u64 nextPersistentInfoSequence = 1;
    	u64 persistentInfoDropped = 0;

    	bool isThreaded = false;
    	u16 threadId {};
    };

	void terminalThreadFunction();
}

#endif
