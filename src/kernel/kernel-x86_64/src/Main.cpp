#include "Main.hpp"
#include "limine.h"

using namespace kernel::x86_64;

extern "C" void kernelMain() {
    Kernel();
}

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile limine_framebuffer_request framebufferRequest = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_memmap_request memMapRequest = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0,
	.response = nullptr,
};

namespace kernel::x86_64 {
	Kernel::Kernel() {
		asm volatile("mov %0, %%rsp" : "=r"(stackTop));
	}

	void Kernel::init() {
		if (LIMINE_BASE_REVISION_SUPPORTED == false) {
			this->halt();
		}

		if (framebufferRequest.response == nullptr || framebufferRequest.response->framebuffer_count < 1) {
			this->halt();
		}

		limine_framebuffer *framebuffer = framebufferRequest.response->framebuffers[0];

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

		// Memory
		this->pagingManager = VirtualMemoryManager();

		this->halt();
    }

	void Kernel::halt() {
    	for (;;) {
    		asm volatile ("hlt");
    	}
    }
}