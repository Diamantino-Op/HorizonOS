#include "Terminal.hpp"

#include "backends/fb.h"
#include "stdarg.h"

namespace kernel::common {
	 Terminal::Terminal(limine_framebuffer *framebuffer) {
	 	this->flantermCtx = flanterm_fb_init(
	 		nullptr,
	 		nullptr,
	 		(u32 *) framebuffer->address,
	 		framebuffer->width,
	 		framebuffer->height,
	 		framebuffer->pitch,
	 		framebuffer->red_mask_size,
	 		framebuffer->red_mask_shift,
	 		framebuffer->green_mask_size,
	 		framebuffer->green_mask_shift,
	 		framebuffer->blue_mask_size,
	 		framebuffer->blue_mask_shift,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		0,
	 		0,
	 		1,
	 		0,
	 		0,
	 		0);
	}

	void Terminal::putChar(char c) {
	 	char str[] = { c };

	 	flanterm_write(this->flantermCtx, str, sizeof(str));
	}

	void Terminal::putString(const char *str) {
	 	flanterm_write(this->flantermCtx, str, sizeof(str));
	}

	void Terminal::printf(const char *format, ...) {
		i32* argp = reinterpret_cast<i32 *>(&format) + 1;
		i32 state = PRINTF_STATE_NORMAL;
		i32 length = PRINTF_LENGTH_DEFAULT;
		i32 radix = 10;
		bool sign = false;

		while (*format) {
			switch (state) {
				case PRINTF_STATE_NORMAL:
					switch (*format) {
						case '%':
							state = PRINTF_STATE_LENGTH;
							break;

						default:
							putChar(*format);
							break;
					}

					break;

				case PRINTF_STATE_LENGTH:
					switch (*format) {
						case 'h':
							length = PRINTF_LENGTH_SHORT;
							state = PRINTF_STATE_LENGTH_SHORT;
							break;

						case 'l':
							length = PRINTF_LENGTH_LONG;
							state = PRINTF_STATE_LENGTH_LONG;
							break;

						default:
							goto PRINTF_STATE_SPEC_GOTO;
					}

					break;

				case PRINTF_STATE_LENGTH_SHORT:
					if (*format == 'h') {
						length = PRINTF_LENGTH_SHORT_SHORT;
						state = PRINTF_STATE_SPEC;
					} else {
						goto PRINTF_STATE_SPEC_GOTO;
					}

					break;

				case PRINTF_STATE_LENGTH_LONG:
					if (*format == 'l') {
						length = PRINTF_LENGTH_LONG_LONG;
						state = PRINTF_STATE_SPEC;
					} else {
						goto PRINTF_STATE_SPEC_GOTO;
					}

					break;

				case PRINTF_STATE_SPEC:
					PRINTF_STATE_SPEC_GOTO:
						switch (*format) {
							case 'c':
								putChar(static_cast<char>(*argp));
								argp++;
								break;

							case 's':
								putString(*reinterpret_cast<char **>(argp));
								argp++;
								break;

							case '%':
								putChar('%');
								break;

							case 'd':
							case 'i':
								radix = 10;
								sign = true;
								argp = printfNumber(argp, length, sign, radix);
								break;

							case 'u':
								radix = 10;
								sign = false;
								argp = printfNumber(argp, length, sign, radix);
								break;

							case 'x':
							case 'X':
							case 'p':
								radix = 16;
								sign = false;
								argp = printfNumber(argp, length, sign, radix);
								break;

							case 'o':
								radix = 8;
								sign = false;
								argp = printfNumber(argp, length, sign, radix);
								break;

							default:
								break;
						}

						state = PRINTF_STATE_NORMAL;
						length = PRINTF_LENGTH_DEFAULT;
						radix = 10;
						sign = false;
						break;

				default:
					break;
			}

			format++;
		}
	}

	i32 *Terminal::printfNumber(i32 *argp, i32 length, bool sign, i32 radix) {
	    char buffer[32];
		u128 number = 0;
		i32 numberSign = 1;
		i32 pos = 0;

		switch (length) {
			case PRINTF_LENGTH_SHORT_SHORT:
			case PRINTF_LENGTH_SHORT:
			case PRINTF_LENGTH_DEFAULT:
				if (sign) {
					i32 n = *argp;

					if (n < 0) {
						n = -n;
						numberSign = -1;
					}

					number = (u32) n;
				} else {
					number = *(u32*) argp;
				}

				argp++;
				break;

			case PRINTF_LENGTH_LONG_LONG:
				if (sign) {
					i128 n = *reinterpret_cast<i128 *>(argp);

					if (n < 0) {
						n = -n;
						numberSign = -1;
					}

					number = (u128) n;
				} else {
					number = *(u128*) argp;
				}

				argp++;
				break;

			case PRINTF_LENGTH_LONG:
				if (sign) {
					i64 n = *(i64 *) argp;

					if (n < 0) {
						n = -n;
						numberSign = -1;
					}

					number = (u128) n;
				} else {
					number = *(u64*) argp;
				}

				argp += 4;
				break;

			default:
				break;
		}

		do {
			u32 rem = number % radix;
			number = number / radix;
			buffer[pos++] = HEX_DIGITS[rem];
		} while (number > 0);

		if (sign && numberSign < 0) {
			buffer[pos] = '-';
		}

		for (i32 i = 0; i <= pos; i++) {
			putChar(buffer[i]);
		}

		return argp;
    }

	char* Terminal::getFormat(const char* mainFormat, ...) {
	 	va_list args;
	 	va_start(args, mainFormat);

	 	size_t totalLength = 0;
	 	const char* temp = FORMAT_CHAR;
	 	while (*temp) {
	 		totalLength++;
	 		temp++;
	 	}

	 	const char* arg = mainFormat;
	 	while (arg) {
	 		temp = arg;
	 		while (*temp) {
	 			totalLength++;
	 			temp++;
	 		}
	 		arg = va_arg(args, const char*);
	 		if (arg) totalLength++;
	 	}
	 	va_end(args);

	 	char* result = new char[totalLength + 1];
	 	if (!result) return nullptr;

	 	char* ptr = result;
	 	temp = FORMAT_CHAR;
	 	while (*temp) {
	 		*ptr = *temp;
	 		ptr++;
	 		temp++;
	 	}

	 	va_start(args, mainFormat);
	 	arg = mainFormat;
	 	while (arg) {
	 		while (*arg) {
	 			*ptr = *arg;
	 			ptr++;
	 			arg++;
	 		}
	 		arg = va_arg(args, const char*);
	 		if (arg) {
	 			*ptr = ',';
	 			ptr++;
	 		}
	 	}
	 	*ptr = '\0';
	 	va_end(args);

	 	return result;
	}

	const char* Terminal::getTextFormatting(TextFormatting format) {
	 	switch (format) {
	 		case TextFormatting::Regular: return "0";
	 		case TextFormatting::Bold: return "1";
	 		case TextFormatting::LowIntensity: return "2";
	 		case TextFormatting::Italic: return "3";
	 		case TextFormatting::Underline: return "4";
	 		case TextFormatting::Blinking: return "5";
	 		case TextFormatting::Reverse: return "6";
	 		case TextFormatting::Background: return "7";
	 		case TextFormatting::Invisible: return "8";
	 		default: return "0";
	 	}
    }

	const char* Terminal::getTextColor(TextColor color) {
	 	switch (color) {
	 		case TextColor::Black: return "30m";
	 		case TextColor::Red: return "31m";
	 		case TextColor::Green: return "32m";
	 		case TextColor::Yellow: return "33m";
	 		case TextColor::Blue: return "34m";
	 		case TextColor::Magenta: return "35m";
	 		case TextColor::Cyan: return "36m";
	 		case TextColor::White: return "37m";
	 		default: return "0";
	 	}
	 }

	const char* Terminal::getBackgroundColor(TextColor color) {
	 	switch (color) {
	 		case TextColor::Black: return "40m";
	 		case TextColor::Red: return "41m";
	 		case TextColor::Green: return "42m";
	 		case TextColor::Yellow: return "43m";
	 		case TextColor::Blue: return "44m";
	 		case TextColor::Magenta: return "45m";
	 		case TextColor::Cyan: return "46m";
	 		case TextColor::White: return "47m";
	 		default: return "0";
	 	}
	 }
}