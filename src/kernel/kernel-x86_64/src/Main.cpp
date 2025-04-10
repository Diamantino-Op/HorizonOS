#include "Main.hpp"
#include "limine.h"

using namespace kernel::x86_64;

Terminal Kernel::terminal;

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
static volatile limine_hhdm_request hhdmRequest = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_memmap_request memMapRequest = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_5_level_paging_request level5PagingRequest = {
	.id = LIMINE_5_LEVEL_PAGING_REQUEST,
	.revision = 0,
	.response = nullptr,
};

namespace kernel::x86_64 {
    Kernel::Kernel() {
    	if (LIMINE_BASE_REVISION_SUPPORTED == false) {
    		halt();
    	}

    	if (framebufferRequest.response == nullptr || framebufferRequest.response->framebuffer_count < 1) {
    		halt();
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
    	this->pagingManager = PagingManager<PageTable>();

    	halt();
    }

	void Kernel::halt() {
    	for (;;) {
    		asm volatile ("hlt");
    	}
    }

	Terminal* Kernel::getTerminal() {
		return &terminal;
    }
}