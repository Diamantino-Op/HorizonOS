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

        static void putChar(char c, void *ctx);
        static void putCharNF(char c, void *ctx);
        void printf(bool autoSN, const char* format, ...);
        void printfNF(bool autoSN, const char* format, ...);
        void printfLock(bool autoSN, const char* format, ...);

        void info(const char *format, const char *id, ...);
        void debug(const char *format, const char *id, ...);
        void debugNS(const char *format, const char *id, ...);
        void debugNF(const char *format, const char *id, ...);
        void warn(const char *format, const char *id, ...);
        void warnNoLock(const char *format, const char *id, ...);
        void error(const char *format, const char *id, ...);
        void errorNoLock(const char *format, const char *id, ...);

        ExecutionNode *getCurrentCore();

    private:
        static flanterm_context *flantermCtx;

        TicketSpinLock spinLock;
    };
}

#endif