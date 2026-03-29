#include "Main.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"
#include "Time.hpp"
#include "programs/Elf.hpp"
#include "hal/Syscall.hpp"

#include "limine.h"

__attribute__((used, section(".limine_requests_start")))
static volatile u64 limineRequestsEndMarker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile u64 limineRequestsStartMarker[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile u64 limineBaseRevision[] = LIMINE_BASE_REVISION(3);

extern limine_framebuffer_request framebufferRequest;
extern limine_module_request moduleRequest;

extern "C" __attribute__((no_instrument_function)) void kernelMain(const u64 rsp) {
    auto kernel = kernel::x86_64::Kernel(rsp);

	kernel.init();
}

namespace kernel::x86_64 {
	using namespace utils;
	using namespace common::programs;

	Kernel::Kernel(const u64 rsp) {
		this->stackTop = rsp;
	}

	void Kernel::init() {
		this->rootInit();

		if (LIMINE_BASE_REVISION_SUPPORTED(limineBaseRevision) == false) {
			Asm::lhlt();
		}

		if (framebufferRequest.response == nullptr or framebufferRequest.response->framebuffer_count < 1) {
			Asm::lhlt();
		}

		limine_framebuffer *framebuffer = framebufferRequest.response->framebuffers[0];

		// Terminal
		terminal = Terminal(framebuffer);

		terminal.info("Initializing HorizonOS...", "HorizonOS");

		// GDT
		this->gdtManager = GdtManager(this->tssManager.getTss());

		terminal.info("GDT Created... OK", "HorizonOS");

		this->gdtManager.loadGdt();
		this->gdtManager.reloadRegisters();

		terminal.info("GDT Loaded... OK", "HorizonOS");

		this->tssManager.updateTss();

		terminal.info("Updated TSS... OK", "HorizonOS");

		// Exceptions
		for (u16 i = 0; i <= 31; i++) {
			this->idtManager.addEntry(i, interruptTable[i], Selector::KERNEL_CODE, 2, GateDPL::KERNEL_DPL | GateType::TRAP_GATE);
		}

		// NMI
		this->idtManager.addEntry(2, interruptTable[2], Selector::KERNEL_CODE, 1, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);

		// Interrupts
		for (u16 i = 32; i <= 255; i++) {
			this->idtManager.addEntry(i, interruptTable[i], Selector::KERNEL_CODE, 0, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);
		}

		this->idtManager.addEntry(0x21, interruptTable[0x21], Selector::KERNEL_CODE, 3, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);
		this->idtManager.addEntry(0x80, interruptTable[0x80], Selector::USER_CODE32, 3, GateDPL::USER_DPL | GateType::INTERRUPT_GATE);

		this->idtManager.loadIdt();

		terminal.info("IDT Loaded... OK", "HorizonOS");

		// PIC
		this->dualPic.init();

		terminal.info("PIC Initialised... OK", "HorizonOS");

		// Physical Memory
		this->physicalMemoryManager.init();

		terminal.info("Total Usable Memory: %llu", "HorizonOS", this->physicalMemoryManager.getFreeMemory());

		// Allocator Context
		this->kernelAllocContextHHDM = VirtualAllocator::createContext();

		this->kernelAllocContext = this->kernelAllocContextHHDM;

		terminal.info("Allocator Context created...", "HorizonOS");

		// Virtual Memory
		this->virtualMemoryManager = VirtualMemoryManager(this->stackTop);

		this->virtualMemoryManager.archInit();

		this->kernelAllocContext = reinterpret_cast<AllocContext *>(reinterpret_cast<u64>(this->kernelAllocContext->heapStart) - sizeof(AllocContext));

		terminal.info("VMM Loaded... OK", "HorizonOS");

		VirtualAllocator::initContext(this->kernelAllocContext);

		terminal.info("Allocator Context initialized...", "HorizonOS");

		this->virtualPageAllocator.init(this->virtualMemoryManager.getVirtualKernelAddr());

		terminal.info("Virtual Page Allocator initialized...", "HorizonOS");

		// Tss Stack

		this->tssManager.allocStack();

		// Cpu Init
		this->cpuManager.init();

		this->cpuManager.startBootCore();

		terminal.debug("Is running under a VM: %u", "HorizonOS", CpuId::isHypervisor());
		terminal.debug("Kvm Base: 0x%.8lx", "HorizonOS", CpuId::getKvmBase());

		CpuManager::initSimd();

		terminal.info("Cpu initialized...", "HorizonOS");

		// Scheduler

		this->scheduler = new Scheduler();

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
		this->kvmClock.init();

		terminal.info("Kvm Clock Initialised... OK", "HorizonOS");

		// Acpi PM Clock
		this->acpiPM.init();

		terminal.info("AcpiPM Clock Initialised... OK", "HorizonOS");

		// Start of multicore

		terminal.info("SIMD Initialised... OK", "HorizonOS");

		this->scheduler->initArch();

		CpuManager::getCurrentCore()->executionNode.init();

		/*auto *exampleProcess = new Process(ProcessPriority::NORMAL, false);
		this->scheduler->addProcess(exampleProcess);

		this->scheduler->addThread(false, reinterpret_cast<u64>(thread1), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread2), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread3), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread4), exampleProcess);
		this->scheduler->addThread(false, reinterpret_cast<u64>(thread5), exampleProcess);*/

		for (u64 i = 0; i < moduleRequest.response->module_count; i++) {
			const limine_file *moduleFile = moduleRequest.response->modules[i];

			terminal.info("Module %u: %s Size: %u", "HorizonOS", i, moduleFile->path, moduleFile->size);

			if (Elf::isElf(static_cast<ElfCommonHeader *>(moduleFile->address))) {
				terminal.info("Loading module %u as ELF...", "HorizonOS", i);

				auto *moduleProcess = new Process(ProcessPriority::NORMAL, true);
				this->scheduler->addProcess(moduleProcess);
			}
		}

		//auto *exampleUserProcess = new Process(ProcessPriority::HIGH, true);
		//this->scheduler->addProcess(exampleUserProcess);

		//this->scheduler->addThread(true, reinterpret_cast<u64>(testUserThread), exampleUserProcess);

		terminal.info("Example threads registered... OK", "HorizonOS");

		// Tsc

		this->cpuManager.getBootstrapCpu()->tsc.init();
		this->cpuManager.getBootstrapCpu()->tsc.globalInit();

		terminal.info("TSC Initialised... OK", "HorizonOS");

		// Apic

		this->cpuManager.getBootstrapCpu()->apic.init();

		this->isInitFlag = true;

		Asm::sti();

		// Init uAcpi

		this->uAcpi.init();

		terminal.info("uACPI Initialised... OK", "HorizonOS");

		SyscallManager::init();

		terminal.info("Syscalls Initialised... OK", "HorizonOS");

		// Multithread

		this->cpuManager.startMultithread();

		terminal.info("All Cpus initialized...", "HorizonOS");

		// Todo: make one shot and restart when thread goes to sleep
		//this->cpuManager.getBootstrapCpu()->apic.arm(TimeUtils::msToNs(50), 0x21, true);

		// this->shutdown();

		Asm::lhlt();
	}

	void testUserThread() {
		for (;;) {
			Asm::cli();
		}
	}

	void thread1() {
		 for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			CommonMain::getTerminal()->warn("Call NS: %llu", "Thread 1", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 100ull * 1'000'000ull); // 10 ms
		}
	}

	void thread2() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			CommonMain::getTerminal()->warn("Call NS: %llu", "Thread 2", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 200ull * 1'000'000ull); // 20 ms
		}
	}

	void thread3() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			CommonMain::getTerminal()->warn("Call NS: %llu", "Thread 3", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 300ull * 1'000'000ull); // 30 ms
		}
	}

	void thread4() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			CommonMain::getTerminal()->warn("Call NS: %llu", "Thread 4", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 400ull * 1'000'000ull); // 40 ms
		}
	}

	void thread5() {
		for (;;) {
			const u64 ns = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

			CommonMain::getTerminal()->warn("Call NS: %llu", "Thread 5", ns);

			auto *currThread = reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));

			CommonMain::getInstance()->getScheduler()->sleepThread(currThread, 50ull * 1'000'000ull); // 50 ms
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
		auto *commonKernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		Terminal* terminal = CommonMain::getTerminal();

		this->coreGdtManager = GdtManager(this->coreTssManager.getTss());

		this->coreGdtManager.loadGdt();
		this->coreGdtManager.reloadRegisters();

		this->coreTssManager.allocStack();

		this->coreTssManager.updateTss();

		SyscallManager::init();

		this->coreIdtManager = commonKernel->getIdtManager();

		this->coreIdtManager->loadIdt();

		CpuManager::initSimd();

		CpuManager::getCurrentCore()->executionNode.init();

		this->cpuCore.tsc.init();

		this->cpuCore.apic.init();

		Asm::sti();

		//this->cpuCore.apic.arm(TimeUtils::msToNs(50), 0x21, true);

		terminal->info("Core %u initialized...", "Cpu", this->cpuCore.cpuId);

		Asm::lhlt();
	}

	TssManager *CoreKernel::getTssManager() {
		return &this->coreTssManager;
	}
}
