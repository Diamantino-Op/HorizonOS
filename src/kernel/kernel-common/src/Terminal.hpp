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

    class Terminal {
    public:
        Terminal() = default;

        explicit Terminal(const limine_framebuffer *framebuffer);

        __attribute__((no_instrument_function)) bool lock();
        __attribute__((no_instrument_function)) void unlock(bool prevIF);

        __attribute__((no_instrument_function)) static void putChar(int c, void *ctx);
        __attribute__((no_instrument_function)) static void putCharE9(int c, void *ctx);
        __attribute__((no_instrument_function)) static void putCharBoth(int c, void *ctx);

        __attribute__((no_instrument_function)) void printf(bool autoSN, const char* format, ...);
        __attribute__((no_instrument_function)) void printfE9(bool autoSN, const char* format, ...);
        __attribute__((no_instrument_function)) void printfBoth(bool autoSN, const char* format, ...);
        __attribute__((no_instrument_function)) void printfUAcpi(bool autoSN, const char* format, ...);
        __attribute__((no_instrument_function)) void printInterruptFrame(u64 *framePtr);

        __attribute__((no_instrument_function)) void info(const char *format, const char *id, ...);
        __attribute__((no_instrument_function)) void debug(const char *format, const char *id, ...);
        __attribute__((no_instrument_function)) void warn(const char *format, const char *id, ...);
        __attribute__((no_instrument_function)) void warnNoLock(const char *format, const char *id, ...);
        __attribute__((no_instrument_function)) void error(const char *format, const char *id, ...);

        ExecutionNode *getCurrentCore();

    private:
        static flanterm_context *flantermCtx;

        TicketSpinLock spinLock;

    public:
    	LFQueue<TermMsg, maxMessages> msgQueue;

    	bool isThreaded = false;
    };

	__attribute__((no_instrument_function, noreturn)) void terminalThreadFunction();
}

#endif