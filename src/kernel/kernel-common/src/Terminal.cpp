#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0

#define NANOPRINTF_IMPLEMENTATION

#include "Terminal.hpp"

#include "flanterm_backends/fb.h"
#include "nanoprintf.h"
#include "stdarg.h"

#include "memory/MainMemory.hpp"

#include "CommonMain.hpp"

namespace kernel::common {
	using namespace memory;

	flanterm_context *Terminal::flantermCtx;

	Terminal::Terminal(const limine_framebuffer *framebuffer) {
		u32 bgColor = 0x252525;

	 	flantermCtx = flanterm_fb_init(
	 		nullptr,
	 		nullptr,
	 		static_cast<u32 *>(framebuffer->address),
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
	 		&bgColor,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		nullptr,
	 		0,
	 		0,
	 		1,
	 		0,
	 		0,
	 		0,
	 		0);
	}

	bool Terminal::lock() {
		if (CommonMain::getInstance()->isInit()) {
			this->getCurrentCore()->setDisabled(true);
		}

		//return this->spinLock.lock();

		return false;
	}

	void Terminal::unlock(const bool prevIF) {
		//this->spinLock.unlock(this->prevIF);

		if (CommonMain::getInstance()->isInit()) {
			this->getCurrentCore()->setDisabled(false);
		}
	}

	void Terminal::putChar(int c, void *) {
		constexpr u16 com1Port = 0x3F8;

	 	if (flantermCtx != nullptr) {
	 		flanterm_write(flantermCtx, reinterpret_cast<const char *>(&c), 1);
	 	}

		// TODO: Only x86_64
		asm volatile ("outb %0, %1" : : "a"(static_cast<char>(c)), "d"(com1Port));
	}

	void Terminal::putCharE9(int c, void *) {
		constexpr u16 e9Port = 0xe9;

		// TODO: Only x86_64
		asm volatile ("outb %0, %1" : : "a"(static_cast<char>(c)), "d"(e9Port));
	}

	void Terminal::putCharBoth(int c, void *) {
		constexpr u16 e9Port = 0xe9;

		if (flantermCtx != nullptr) {
			flanterm_write(flantermCtx, reinterpret_cast<const char *>(&c), 1);
		}

		// TODO: Only x86_64
		asm volatile ("outb %0, %1" : : "a"(static_cast<char>(c)), "d"(e9Port));
	}

	void Terminal::printf(const bool autoSN, const char *format, ...) {
	 	va_list val;
	 	va_start(val, format);
	 	npf_vpprintf(putChar, nullptr, format, val);
	 	va_end(val);

		if (autoSN) {
			npf_pprintf(putChar, nullptr, "\r\n");
		}
	}

	void Terminal::printfE9(const bool autoSN, const char *format, ...) {
		va_list val;
		va_start(val, format);
		npf_vpprintf(putCharE9, nullptr, format, val);
		va_end(val);

		if (autoSN) {
			npf_pprintf(putCharE9, nullptr, "\r\n");
		}
	}

	void Terminal::printfBoth(const bool autoSN, const char *format, ...) {
		va_list val;
		va_start(val, format);
		npf_vpprintf(putCharBoth, nullptr, format, val);
		va_end(val);

		if (autoSN) {
			npf_pprintf(putCharBoth, nullptr, "\r\n");
		}
	}

	void Terminal::printfUAcpi(const bool autoSN, const char *format, ...) {
		const bool prevIF = this->lock();

		va_list val;
		va_start(val, format);
		npf_vpprintf(putChar, nullptr, format, val);
		va_end(val);

		if (autoSN) {
			npf_pprintf(putChar, nullptr, "\r\n");
		}

		this->unlock(prevIF);
	}

	void Terminal::info(const char *format, const char *id, ...) {
		const bool prevIF = this->lock();

		this->printf(false, "[ \o{33}[1;34minformation \o{33}[0m] \o{33}[1;30m%s: \o{33}[0;37m", id);

		va_list val;
		va_start(val, id);
		npf_vpprintf(putChar, nullptr, format, val);
		va_end(val);

		this->printf(true, "\033[0m");

		this->unlock(prevIF);
	}

	void Terminal::debug(const char *format, const char *id, ...) {
#ifdef HORIZON_DEBUG
		const bool prevIF = this->lock();

		this->printfE9(false, "[    \o{33}[0;32mdebug    \o{33}[0m] \o{33}[1;30m%s: \o{33}[0;37m", id);

		va_list val;
		va_start(val, id);
		npf_vpprintf(putCharE9, nullptr, format, val);
		va_end(val);

		this->printfE9(true, "\033[0m");

		this->unlock(prevIF);
#endif
	}

	void Terminal::warn(const char *format, const char *id, ...) {
		const bool prevIF = this->lock();

		this->printf(false, "[   \o{33}[0;33mwarning   \o{33}[0m] \o{33}[1;30m%s: \o{33}[0;37m", id);

		va_list val;
		va_start(val, id);
		npf_vpprintf(putChar, nullptr, format, val);
		va_end(val);

		this->printf(true, "\033[0m");

		this->unlock(prevIF);
	}

	void Terminal::warnNoLock(const char *format, const char *id, ...) {
		this->printfE9(false, "[   \o{33}[0;33mwarning   \o{33}[0m] \o{33}[1;30m%s: \o{33}[0;37m", id);

		va_list val;
		va_start(val, id);
		npf_vpprintf(putCharE9, nullptr, format, val);
		va_end(val);

		this->printfE9(true, "\033[0m");
	}

	void Terminal::error(const char *format, const char *id, ...) {
		const bool prevIF = this->lock();

		this->printfBoth(false, "[    \o{33}[0;31merror    \o{33}[0m] \o{33}[1;30m%s: \o{33}[0;37m", id);

		va_list val;
		va_start(val, id);
		npf_vpprintf(putCharBoth, nullptr, format, val);
		va_end(val);

		this->printfBoth(true, "\033[0m");

		this->unlock(prevIF);
	}

	/*char* Terminal::getFormat(const char* mainFormat, ...) {
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
	}*/

	/*const char* Terminal::getTextFormatting(TextFormatting format) {
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
    }*/

	/*const char* Terminal::getTextColor(TextColor color) {
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
	}*/

	/*const char* Terminal::getBackgroundColor(TextColor color) {
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
	}*/
}