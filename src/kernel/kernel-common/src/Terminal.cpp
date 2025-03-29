#include "Terminal.hpp"

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
}