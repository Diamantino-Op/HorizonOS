#include "Main.hpp"

#include "hal/Interrupts.hpp"

#include "limine.h"

using namespace kernel::x86_64;

extern "C" void kernelMain() {
    Kernel kernel = Kernel();

	kernel.init();
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

namespace kernel::x86_64 {
	Kernel::Kernel() {
		asm volatile("mov %%rsp, %0" : "=r"(this->stackTop));
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

		terminal.info("Initializing HorizonOS...", "HorizonOS");

		// TSS
		this->tssManager = TssManager();

		terminal.info("TSS Created... OK", "HorizonOS");

		// GDT
		this->gdtManager = GdtManager(this->tssManager.getTss());

		terminal.info("GDT Created... OK", "HorizonOS");

		this->gdtManager.loadGdt();
		this->gdtManager.reloadRegisters();

		terminal.info("GDT Loaded... OK", "HorizonOS");

		this->tssManager.updateTss();

		terminal.info("Updated TSS... OK", "HorizonOS");

		// IDT
		this->idtManager = IdtManager();

		terminal.info("IDT Created... OK", "HorizonOS");

		for (u16 i = 0; i <= 255; i++) {
			this->idtManager.addEntry(i, interruptTable[i], Selector::KERNEL_CODE, 0, GateType::GATE_TYPE);
		}

		this->idtManager.loadIdt();

		terminal.info("IDT Loaded... OK", "HorizonOS");

		// Physical Memory
		this->physicalMemoryManager = PhysicalMemoryManager();

		this->physicalMemoryManager.init();

		terminal.info("Total Usable Memory: %llu", "HorizonOS", this->physicalMemoryManager.getFreeMemory());

		// Virtual Memory
		this->virtualMemoryManager = VirtualMemoryManager(this->stackTop, reinterpret_cast<u64 *>(this));

		this->virtualMemoryManager.archInit();

		this->halt();
    }

	void Kernel::halt() {
    	for (;;) {
    		asm volatile ("hlt");
    	}
    }

	GdtManager *Kernel::getGdtManager() {
		return &this->gdtManager;
	}

	TssManager *Kernel::getTssManager() {
		return &this->tssManager;
	}

	IdtManager *Kernel::getIdtManager() {
		return &this->idtManager;
	}

	PhysicalMemoryManager *Kernel::getPMM() {
		return &this->physicalMemoryManager;
	}

	VirtualMemoryManager *Kernel::getVMM() {
		return &this->virtualMemoryManager;
	}
}