#ifndef KERNEL_COMMON_TERMINAL_HPP
#define KERNEL_COMMON_TERMINAL_HPP

#include "Types.hpp"

#include "limine.h"
#include "flanterm.h"

#include "SpinLock.hpp"

#include "threading/Scheduler.hpp"

namespace kernel::common {
    using namespace threading;

    class Terminal {
    public:
        Terminal() = default;

        explicit Terminal(const limine_framebuffer *framebuffer);

        void lock();
        void unlock();

        static void putChar(int c, void *ctx);
        static void putCharE9(int c, void *ctx);
        static void putCharBoth(int c, void *ctx);

        void printf(bool autoSN, const char* format, ...);
        void printfE9(bool autoSN, const char* format, ...);
        void printfBoth(bool autoSN, const char* format, ...);
        void printfUAcpi(bool autoSN, const char* format, ...);

        void info(const char *format, const char *id, ...);
        void debug(const char *format, const char *id, ...);
        void warn(const char *format, const char *id, ...);
        void warnNoLock(const char *format, const char *id, ...);
        void error(const char *format, const char *id, ...);

        ExecutionNode *getCurrentCore();

    private:
        static flanterm_context *flantermCtx;

        bool prevIF;

        TicketSpinLock spinLock;
    };
}

#endif