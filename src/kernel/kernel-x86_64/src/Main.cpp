#include "Main.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
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
	using namespace utils;

	Kernel::Kernel() {
		asm volatile("mov %%rsp, %0" : "=r"(this->stackTop));
	}

	void Kernel::init() {
		this->rootInit();

		if (LIMINE_BASE_REVISION_SUPPORTED == false) {
			Asm::lhlt();
		}

		if (framebufferRequest.response == nullptr or framebufferRequest.response->framebuffer_count < 1) {
			Asm::lhlt();
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

		// Exceptions
		for (u16 i = 0; i <= 31; i++) {
			this->idtManager.addEntry(i, interruptTable[i], Selector::KERNEL_CODE, 0, GateType::TRAP_GATE);
		}

		// NMI
		this->idtManager.addEntry(2, interruptTable[2], Selector::KERNEL_CODE, 0, GateType::INTERRUPT_GATE);

		// Interrupts
		for (u16 i = 32; i <= 255; i++) {
			this->idtManager.addEntry(i, interruptTable[i], Selector::KERNEL_CODE, 0, GateType::INTERRUPT_GATE);
		}

		this->idtManager.loadIdt();

		terminal.info("IDT Loaded... OK", "HorizonOS");

		// PIC

		this->dualPic = DualPIC();

		this->dualPic.init();

		terminal.info("PIC Initialised... OK", "HorizonOS");

		// Physical Memory
		this->physicalMemoryManager = PhysicalMemoryManager();

		this->physicalMemoryManager.init();

		terminal.info("Total Usable Memory: %llu", "HorizonOS", this->physicalMemoryManager.getFreeMemory());

		// Allocator Context
		this->kernelAllocContext = VirtualAllocator::createContext(false, false);

		terminal.info("Allocator Context created...", "HorizonOS");

		// Virtual Memory
		this->virtualMemoryManager = VirtualMemoryManager(this->stackTop);

		this->virtualMemoryManager.archInit();

		terminal.info("VMM Loaded... OK", "HorizonOS");

		VirtualAllocator::initContext(this->kernelAllocContext);

		terminal.info("Allocator Context initialized...", "HorizonOS");

		// Tss Stack

		this->tssManager.allocStack();

		// Scheduler

		this->scheduler = new Scheduler();

		// Cpu Init

		this->cpuManager = CpuManager();

		this->cpuManager.init();

		terminal.info("Cpu initialized...", "HorizonOS");

		this->cpuManager.startMultithread();

		// Start of multicore

		CpuManager::initSimd();

		terminal.info("All Cpus initialized...", "HorizonOS");

		CpuManager::getCurrentCore()->executionNode.init();

		auto *exampleProcess = new Process(ProcessPriority::NORMAL, false);
		this->scheduler->addProcess(exampleProcess);

		this->scheduler->addThread(false, reinterpret_cast<u64>(thread1), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread2), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread3), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread4), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread5), exampleProcess);

		terminal.info("Example threads registered... OK", "HorizonOS");

		if (cpuManager.getBootstrapCpu()->apic.isInitialized()) {
			//this->dualPic.disable();
		}

		// PIT

		this->pit = PIT();

		this->pit.init(1000);

		terminal.info("PIT Initialised... OK", "HorizonOS");

		this->cpuManager.getBootstrapCpu()->tsc.init();
		this->cpuManager.getBootstrapCpu()->tsc.globalInit();

		terminal.info("TSC Initialised... OK", "HorizonOS");

		this->isInitFlag = true;

		Asm::sti();

		/*this->uAcpi = UAcpi();

		this->uAcpi.init();

		terminal.info("uACPI Initialised... OK", "HorizonOS");*/

		//this->shutdown();

		Asm::lhlt();
    }

	void thread1() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warn("Call NS: %u", "Thread 1", ns);

			//auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10);
		}
	}

	void thread2() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warn("Call NS: %u", "Thread 2", ns);

			//auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10);
		}
	}

	void thread3() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warn("Call NS: %u", "Thread 3", ns);

			//auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10);
		}
	}

	void thread4() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warn("Call NS: %u", "Thread 4", ns);

			//auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10);
		}
	}

	void thread5() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warn("Call NS: %u", "Thread 5", ns);

			//auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10);
		}
	}

	void Kernel::shutdown() {
		terminal.info("Shutting down...", "HorizonOS");

		this->uAcpi.shutdown();
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

	DualPIC *Kernel::getDualPic() {
		return &this->dualPic;
	}

	PIT *Kernel::getPIT() {
		return &this->pit;
	}

	CpuManager *Kernel::getCpuManager() {
		return &this->cpuManager;
	}

	// Multicore

	void CoreKernel::init() {
		Terminal* terminal = CommonMain::getTerminal();

		this->coreTssManager = TssManager();
		this->coreGdtManager = GdtManager(this->coreTssManager.getTss());

		this->coreGdtManager.loadGdt();
		this->coreGdtManager.reloadRegisters();

		this->coreTssManager.allocStack();

		this->coreTssManager.updateTss();

		this->coreIdtManager = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIdtManager();

		this->coreIdtManager->loadIdt();

		CpuManager::initSimd();

		CpuManager::getCurrentCore()->executionNode.init();

		this->cpuCore.tsc.init();

		Asm::sti();

		terminal->info("Core %u initialized...", "Cpu", this->cpuCore.cpuId);

		Asm::lhlt();
	}
}
