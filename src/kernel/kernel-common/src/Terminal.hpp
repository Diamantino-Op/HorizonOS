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
    constexpr char HEX_DIGITS[] = "0123456789abcdef";

	constexpr char FORMAT_CHAR[] = "\033[";

    enum TextFormatting {
        Regular = "0",
        Bold = "1",
        LowIntensity = "2",
        Italic = "3",
        Underline = "4",
        Blinking = "5",
        Reverse = "6",
        Background = "7",
        Invisible = "8",
    };

    enum TextColor {

    };

    class Terminal {
    public:
        Terminal() = default;

        explicit Terminal(limine_framebuffer *framebuffer);

        void putChar(char c);
        void putString(const char* str);
        void printf(const char* format, ...);
        i32* printfNumber(i32* argp, i32 length, bool sign, i32 radix);
        char* getFormat(const char* mainFormat, ...);

    private:
        flanterm_context *flantermCtx;
    };
}

#endif