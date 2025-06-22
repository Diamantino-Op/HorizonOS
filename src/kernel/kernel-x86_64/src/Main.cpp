#include "Main.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

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

#ifndef HORIZON_USE_NEW_ALLOCATOR
		VirtualAllocator::initContext(this->kernelAllocContext);
#endif

		terminal.info("Allocator Context initialized...", "HorizonOS");

		this->virtualPageAllocator = VirtualPageAllocator();

		this->virtualPageAllocator.init(this->virtualMemoryManager.getVirtualKernelAddr());

		terminal.info("Virtual Page Allocator initialized...", "HorizonOS");

		// Tss Stack

		this->tssManager.allocStack();

		// Scheduler

		this->scheduler = new Scheduler();

		// Cpu Init

		this->cpuManager = CpuManager();

		this->cpuManager.init();

		this->cpuManager.startBootCore();

		terminal.debug("Is running under a VM: %u", "HorizonOS", CpuId::isHypervisor());
		terminal.debug("Kvm Base: 0x%.8lx", "HorizonOS", CpuId::getKvmBase());

		CpuManager::initSimd();

		terminal.info("Cpu initialized...", "HorizonOS");

		// Early uAcpi

		this->uAcpi = UAcpi();

		this->uAcpi.earlyInit();

		terminal.info("Early uAcpi init... OK", "HorizonOS");

		this->ioApicManager.init();

		terminal.info("IOApics Initialised... OK", "HorizonOS");

		// PIT

		this->pit = PIT();

		this->pit.init(1000);

		terminal.info("PIT Initialised... OK", "HorizonOS");

		// Hpet

		this->hpet = Hpet();

		this->hpet.init();

		terminal.info("Hpet Initialised... OK", "HorizonOS");

		// Kvm Clock

		this->kvmClock = KvmClock();

		this->kvmClock.init();

		terminal.info("Kvm Clock Initialised... OK", "HorizonOS");

		// Acpi PM Clock

		this->acpiPM = AcpiPM();

		this->acpiPM.init();

		terminal.info("AcpiPM Clock Initialised... OK", "HorizonOS");

		// Start of multicore

		terminal.info("SIMD Initialised... OK", "HorizonOS");

		this->scheduler->initArch();

		CpuManager::getCurrentCore()->executionNode.init();

		auto *exampleProcess = new Process(ProcessPriority::NORMAL, false);
		this->scheduler->addProcess(exampleProcess);

		this->scheduler->addThread(false, reinterpret_cast<u64>(thread1), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread2), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread3), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread4), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread5), exampleProcess);

		terminal.info("Example threads registered... OK", "HorizonOS");

		// Tsc

		this->cpuManager.getBootstrapCpu()->tsc.init();
		this->cpuManager.getBootstrapCpu()->tsc.globalInit();

		terminal.info("TSC Initialised... OK", "HorizonOS");

		// Apic

		this->cpuManager.getBootstrapCpu()->apic.init();

		this->isInitFlag = true;

#ifdef HORIZON_USE_NEW_ALLOCATOR
		this->kernelAllocContext->libAlloc->dump();
#endif

		Asm::sti();

		// Init uAcpi

		this->uAcpi.init();

		terminal.info("uACPI Initialised... OK", "HorizonOS");

		// Multithread

		//this->cpuManager.startMultithread();

		terminal.info("All Cpus initialized...", "HorizonOS");

		terminal.info("Apic scheduler interrupt: %u", "HorizonOS", ioApicManager.getMaxRange());

		this->cpuManager.getBootstrapCpu()->apic.arm(50 * 1'000'000, ioApicManager.getMaxRange() + 0x20, true);

		//this->shutdown();

		Asm::lhlt();
    }

	void thread1() {
		for (;;) {
			//const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warnNoLock("Call NS: %u", "Thread 1", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			//CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 10ull * 1'000ull * 1'000'000ull);

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, );
		}
	}

	void thread2() {
		for (;;) {
			//const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warnNoLock("Call NS: %u", "Thread 2", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 20ull * 1'000ull * 1'000'000ull);
		}
	}

	void thread3() {
		for (;;) {
			//const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warnNoLock("Call NS: %u", "Thread 3", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 30ull * 1'000ull * 1'000'000ull);
		}
	}

	void thread4() {
		for (;;) {
			//const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warnNoLock("Call NS: %u", "Thread 4", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 40ull * 1'000ull * 1'000'000ull); // 40 ms
		}
	}

	void thread5() {
		for (;;) {
			//const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			//CommonMain::getTerminal()->warnNoLock("Call NS: %u", "Thread 5", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 50ull * 1'000ull * 1'000'000ull);
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

	KvmClock *Kernel::getKvmClock() {
		return &this->kvmClock;
	}

	Hpet *Kernel::getHpet() {
		return &this->hpet;
	}

	CpuManager *Kernel::getCpuManager() {
		return &this->cpuManager;
	}

	IOApicManager *Kernel::getIOApicManager() {
		return &this->ioApicManager;
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
