#include "Main.hpp"

#include "hal/Interrupts.hpp"
#include "memory/MainMemory.hpp"

#include "limine.h"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

extern limine_framebuffer_request framebufferRequest;

extern "C" void kernelMain() {
    auto kernel = kernel::x86_64::Kernel();

	kernel.init();
}

namespace kernel::x86_64 {
	Kernel::Kernel() {
		asm volatile("mov %%rsp, %0" : "=r"(this->stackTop));
	}

	void Kernel::init() {
		this->rootInit();

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

		// PIC

		this->dualPic = DualPIC();

		this->dualPic.init();

		terminal.info("PIC Initialised... OK", "HorizonOS");

		// PIT

		this->pit = PIT();

		this->pit.init(1000);

		terminal.info("PIT Initialised... OK", "HorizonOS");

		// Physical Memory
		this->physicalMemoryManager = PhysicalMemoryManager();

		this->physicalMemoryManager.init();

		terminal.info("Total Usable Memory: %llu", "HorizonOS", this->physicalMemoryManager.getFreeMemory());

		// Allocator Context
		this->kernelAllocContext = VirtualAllocator::createContext(false);

		terminal.info("Allocator Context created...", "HorizonOS");

		// Virtual Memory
		this->virtualMemoryManager = VirtualMemoryManager(this->stackTop);

		this->virtualMemoryManager.archInit();

		terminal.info("VMM Loaded... OK", "HorizonOS");

		VirtualAllocator::initContext(this->kernelAllocContext);

		terminal.info("Allocator Context initialized...", "HorizonOS");

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
}