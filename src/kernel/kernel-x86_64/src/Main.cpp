#include "Main.hpp"
#include "limine.h"

using namespace kernel::x86_64;

Terminal Kernel::terminal;

extern "C" void kernelMain() {
    Kernel();
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

namespace kernel::x86_64 {
    Kernel::Kernel() {
    	if (LIMINE_BASE_REVISION_SUPPORTED == false) {
    		halt();
    	}

    	if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1) {
    		halt();
    	}

    	limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    	// Terminal
    	terminal = Terminal(framebuffer);

    	terminal.printf("Initializing HorizonOS...\n");

    	// TSS
    	this->tssManager = TssManager();

    	terminal.printf("TSS Created... OK\n");

    	// GDT
    	this->gdtManager = GdtManager(this->tssManager.getTss());

    	terminal.printf("GDT Created... OK\n");

    	this->gdtManager.loadGdt();
    	this->gdtManager.reloadRegisters();

    	terminal.printf("GDT Loaded... OK\n");

    	this->tssManager.updateTss();

    	terminal.printf("Updated TSS... OK\n");

    	// IDT
		this->idtManager = IDTManager();

    	terminal.printf("IDT Created... OK\n");

    	this->idtManager.loadIdt();

    	terminal.printf("IDT Loaded... OK\n");

    	halt();
    }

	void Kernel::halt() {
    	for (;;) {
    		asm volatile ("hlt");
    	}
    }

	Terminal Kernel::getTerminal() {
		return terminal;
    }
}