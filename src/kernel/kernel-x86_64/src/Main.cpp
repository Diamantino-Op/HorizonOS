#include "Main.hpp"
#include "limine.h"

using namespace x86_64;

extern "C" void kernelMain() {
    Kernel kernel = Kernel();

    kernel.init();
}

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

extern "C" void *memcpy(void *dest, const void *src, usize n) {
	u8 *pdest = (u8 *)dest;
	const u8 *psrc = (const u8 *)src;

	for (usize i = 0; i < n; i++) {
		pdest[i] = psrc[i];
	}

	return dest;
}

extern "C" void *memset(void *s, int c, usize n) {
	u8 *p = (u8 *)s;

	for (usize i = 0; i < n; i++) {
		p[i] = (u8)c;
	}

	return s;
}

extern "C" void *memmove(void *dest, const void *src, usize n) {
	u8 *pdest = (u8 *)dest;
	const u8 *psrc = (const u8 *)src;

	if (src > dest) {
		for (usize i = 0; i < n; i++) {
			pdest[i] = psrc[i];
		}
	} else if (src < dest) {
		for (usize i = n; i > 0; i--) {
			pdest[i-1] = psrc[i-1];
		}
	}

	return dest;
}

extern "C" int memcmp(const void *s1, const void *s2, usize n) {
	const u8 *p1 = (const u8 *)s1;
	const u8 *p2 = (const u8 *)s2;

	for (usize i = 0; i < n; i++) {
		if (p1[i] != p2[i]) {
			return p1[i] < p2[i] ? -1 : 1;
		}
	}

	return 0;
}

#define COM1_BASE 0x3F8  // Base address for COM1
#define LSR (COM1_BASE + 5)  // Line Status Register (LSR) offset
#define LSR_THRE (1 << 5)  // Transmitter Holding Register Empty bit (bit 5)

void outb(u16 port, u16 value) {
	asm volatile("outw %0, %1" :: "a"(value), "Nd"(port));
}

u8 inb(u16 port) {
	u8 value;
	asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

void serialInit() {
	// Line Control Register: 0x80 to enable access to baud rate divisor
	outb(COM1_BASE + 3, 0x80); // LCR: Enable divisor latch access
	outb(COM1_BASE + 0, 0x03); // DLL: Low byte of divisor (for 9600 baud)
	outb(COM1_BASE + 1, 0x00); // DLM: High byte of divisor (for 9600 baud)
	outb(COM1_BASE + 3, 0x03); // LCR: 8 data bits, no parity, 1 stop bit
	outb(COM1_BASE + 4, 0x0B); // MCR: Enable IRQs, etc.
}

bool serialIsTransmitEmpty() {
	// Check if the Transmit Holding Register (THR) is empty (bit 5 of LSR)
	return inb(LSR) & LSR_THRE;
}

void serialWrite(u8 data) {
	// Wait for the transmit buffer to be empty
	while (!serialIsTransmitEmpty()) {
		// Busy-wait (can be replaced with a more efficient approach in a real system)
	}

	// Send the byte of data to the serial port
	outb(COM1_BASE, data);
}

void serialPrint(const char *str) {
	while (*str) {
		serialWrite(*str++);
	}
}

namespace x86_64 {
    void Kernel::init() {
		serialInit();

    	serialPrint("Starting HorizonOS...");

    	serialPrint("Init TSS...");

        this->gdtManager = GdtManager();
    	this->tssManager = TssManager();

    	this->tssManager.initTss();
    	this->tssManager.updateTss();

    	serialPrint("TSS Loaded");

    	serialPrint("Init GDT...");

    	this->gdtManager.initGdt(this->tssManager.getTss());
    	this->gdtManager.loadGdt();
    	this->gdtManager.reloadRegisters();

    	serialPrint("GDT Loaded");

    	if (LIMINE_BASE_REVISION_SUPPORTED == false) {
    		halt();
    	}

    	if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) {
    		halt();
		 }

    	limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    	for (usize i = 0; i < 100; i++) {
    		volatile u32 *fb_ptr = (u32*) framebuffer->address;
    		fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    	}

    	halt();
    }

	void Kernel::halt() {
    	for (;;) {
    		asm volatile ("hlt");
    	}
    }
}