#ifndef KERNEL_COMMON_TERMINAL_HPP
#define KERNEL_COMMON_TERMINAL_HPP

#include "Types.hpp"

#include "limine.h"
#include "flanterm.h"

#include "SpinLock.hpp"

#include "threading/Scheduler.hpp"

#define PRINTF_STATE_NORMAL       0
#define PRINTF_STATE_LENGTH       1
#define PRINTF_STATE_LENGTH_LONG  2
#define PRINTF_STATE_LENGTH_SHORT 3
#define PRINTF_STATE_SPEC         4

#define PRINTF_LENGTH_DEFAULT     0
#define PRINTF_LENGTH_SHORT_SHORT 1
#define PRINTF_LENGTH_SHORT       2
#define PRINTF_LENGTH_LONG        3
#define PRINTF_LENGTH_LONG_LONG   4

namespace kernel::common {
    using namespace threading;

    enum TextFormatting {
        Regular,
        Bold,
        LowIntensity,
        Italic,
        Underline,
        Blinking,
        Reverse,
        Background,
        Invisible
    };

    enum TextColor {
        Black,
        Red,
        Green,
        Yellow,
        Blue,
        Magenta,
        Cyan,
        White
    };

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
        void debugNF(const char *format, const char *id, ...);
        void warn(const char *format, const char *id, ...);
        void warnNoLock(const char *format, const char *id, ...);
        void error(const char *format, const char *id, ...);
        void errorNoLock(const char *format, const char *id, ...);

        ExecutionNode *getCurrentCore();

        //char* getFormat(const char* mainFormat, ...);

        //const char* getTextFormatting(TextFormatting format);
        //const char* getTextColor(TextColor format);
        //const char* getBackgroundColor(TextColor format);

    private:
        static flanterm_context *flantermCtx;

        TicketSpinLock spinLock;
    };
}

#endif