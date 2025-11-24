#include "Types.hpp"

/*extern "C" __attribute__((no_instrument_function)) void mcount() {
	u64 retAddr;

	asm volatile("movq 8(%%rbp), %0" : "=r"(retAddr));

	for (int i = 0; i < 8; i++) {
		constexpr u16 com2Port = 0x2F8;

		char c = (retAddr >> (i * 8)) & 0xFF;

		asm volatile ("outb %0, %1" : : "a"(c), "d"(com2Port));
	}
}*/

#define COM2_BASE 0x2F8
#define COM2_DATA_PORT (COM2_BASE + 0)
#define COM2_LINE_STATUS (COM2_BASE + 5)
#define COM2_LINE_STATUS_THRE 0x20

__attribute__((no_instrument_function)) void outb(u16 port, u8 val) {
	asm volatile ("out %1, %0" : : "a"(val), "Nd"(port));
}

__attribute__((no_instrument_function)) u8 inb(u16 port) {
	u8 ret;

	asm volatile ("in %0, %1" : "=a"(ret) : "Nd"(port));

	return ret;
}

__attribute__((no_instrument_function)) void uartWaitForTransmit() {
	while ((inb(COM2_LINE_STATUS) & COM2_LINE_STATUS_THRE) == 0) {}
}

__attribute__((no_instrument_function))void uartSendChar(char c) {
	uartWaitForTransmit();

	outb(COM2_DATA_PORT, c);
}

__attribute__((no_instrument_function)) void uartSendString(const char* str) {
	while (*str) {
		uartSendChar(*str++);
	}
}

__attribute__((no_instrument_function)) char nibbleToHex(u8 nibble) {
	return nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10));
}

__attribute__((no_instrument_function)) void uartSendHex(u64 val) {
	for (int i = 15; i >= 0; i--) {
		u8 nibble = (val >> (i * 4)) & 0xF;
		uartSendChar(nibbleToHex(nibble));
	}
}


extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *thisFn, void *callSite) {
	// Serialize and send function enter event over UART
	uartSendString("Enter: ");
	uartSendHex(reinterpret_cast<uPtr>(thisFn));
	uartSendString("\n");
}

extern "C"  __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *thisFn, void *callSite) {
	// Serialize and send function exit event over UART
	uartSendString("Exit: ");
	uartSendHex(reinterpret_cast<uPtr>(thisFn));
	uartSendString("\n");
}