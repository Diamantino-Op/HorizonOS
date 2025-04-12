#ifndef KERNEL_COMMON_TERMINAL_HPP
#define KERNEL_COMMON_TERMINAL_HPP

#include "Types.hpp"

#include "limine.h"
#include "flanterm.h"

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

        static void putChar(char c, void *ctx);
        void printf(const char* format, ...);
        //char* getFormat(const char* mainFormat, ...);

        //const char* getTextFormatting(TextFormatting format);
        //const char* getTextColor(TextColor format);
        //const char* getBackgroundColor(TextColor format);

    private:
        static flanterm_context *flantermCtx;
    };
}

#endif