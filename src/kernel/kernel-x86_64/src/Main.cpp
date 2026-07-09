#include "Main.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"
#include "Time.hpp"
#include "programs/Elf.hpp"
#include "hal/Syscall.hpp"
#include "memory/MainMemory.hpp"

#include "limine.h"

#include <new>

// ReSharper disable CppDeclaratorNeverUsed

__attribute__((used, section(".limine_requests_start")))
static volatile u64 limineRequestsEndMarker[] = LIMINE_REQUESTS_START_MARKER; // NOLINT(*-use-anonymous-namespace, *-avoid-c-arrays)

__attribute__((used, section(".limine_requests_end")))
static volatile u64 limineRequestsStartMarker[] = LIMINE_REQUESTS_END_MARKER; // NOLINT(*-use-anonymous-namespace, *-avoid-c-arrays)

__attribute__((used, section(".limine_requests")))
static volatile u64 limineBaseRevision[] = LIMINE_BASE_REVISION(6); // NOLINT(*-use-anonymous-namespace, *-avoid-c-arrays)

// ReSharper enable CppDeclaratorNeverUsed

extern limine_framebuffer_request framebufferRequest;
extern limine_module_request moduleRequest;

namespace {
	alignas(kernel::x86_64::Kernel) u8 kernelStorage[sizeof(kernel::x86_64::Kernel)];

	auto stringEquals(const char *left, const char *right) -> bool {
		if (left == nullptr || right == nullptr) {
			return false;
		}

		while (*left != '\0' && *right != '\0') {
			if (*left != *right) {
				return false;
			}

			++left;
			++right;
		}

		return *left == '\0' && *right == '\0';
	}
}

extern "C" __attribute__((no_instrument_function)) void kernelMain(const u64 rsp) {
    auto *kernel = new (kernelStorage) kernel::x86_64::Kernel(rsp);

	kernel->init();
}

namespace kernel::x86_64 {
	using namespace utils;
	using namespace common::programs;

	Kernel::Kernel(const u64 rsp) {
		this->stackTop = rsp;
	}

	void Kernel::init() {
		if (not LIMINE_BASE_REVISION_SUPPORTED(limineBaseRevision)) {
			Asm::lhlt();
		}

		if (framebufferRequest.response == nullptr or framebufferRequest.response->framebuffer_count < 1) {
			Asm::lhlt();
		}

		const limine_framebuffer *framebuffer = framebufferRequest.response->framebuffers[0];

		// Terminal
		terminal = Terminal(framebuffer);

		terminal.info("Initializing HorizonOS...", "HorizonOS");

		this->rootInit();

		// GDT
		this->gdtManager = GdtManager(this->tssManager.getTss());

		terminal.info("GDT Created... OK", "HorizonOS");

		this->gdtManager.loadGdt();
		this->gdtManager.reloadRegisters();

		terminal.info("GDT Loaded... OK", "HorizonOS");

		TssManager::updateTss();

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

		this->idtManager.addEntry(0x80, interruptTable[0x80], Selector::USER_CODE32, 0, GateDPL::USER_DPL | GateType::INTERRUPT_GATE);

		this->idtManager.loadIdt();

		terminal.info("IDT Loaded... OK", "HorizonOS");

		// PIC
		// TODO: PIC won't be used
		//this->dualPic.init();

		//terminal.info("PIC Initialised... OK", "HorizonOS");

		// Physical Memory
		this->physicalMemoryManager.init();

		terminal.info("Total Usable Memory: %llu", "HorizonOS", this->physicalMemoryManager.getFreeMemory());

		// Allocator Context
		this->kernelAllocContextHHDM = VirtualAllocator::createContext();

		this->kernelAllocContext = this->kernelAllocContextHHDM;

		terminal.info("Allocator Context created...", "HorizonOS");

		VirtualMemoryManager::initPAT();

		// Virtual Memory
		this->virtualMemoryManager = VirtualMemoryManager(this->stackTop);

		this->virtualMemoryManager.archInit();

		this->kernelAllocContext = reinterpret_cast<AllocContext *>(reinterpret_cast<u64>(this->kernelAllocContext->heapStart) - sizeof(AllocContext));

		terminal.info("VMM Loaded... OK", "HorizonOS");

		VirtualAllocator::initContext(this->kernelAllocContext);

		terminal.info("Allocator Context initialized...", "HorizonOS");

		this->virtualPageAllocator.init(this->virtualMemoryManager.getVirtualKernelAddr());

		terminal.info("Virtual Page Allocator initialized...", "HorizonOS");

		// TssIopb Stack

		this->tssManager.allocStack();

		// Cpu Init
		this->cpuManager.init();

		this->cpuManager.startBootCore();

		terminal.debug("Is running under a VM: %u", "HorizonOS", static_cast<u8>(CpuId::isHypervisor()));
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

		if (const u64 kvmFreq = this->kvmClock.tscFreq(); kvmFreq != 0) {
			terminal.debug("Timer frequency: %lu Hz", "KvmClock", kvmFreq);
			terminal.info("Kvm Clock Initialised... OK", "HorizonOS");
		} else {
			terminal.debug("Kvm clock frequency unavailable", "KvmClock");
			terminal.info("Kvm Clock unavailable", "HorizonOS");
		}

		// Acpi PM Clock
		this->acpiPM.init();

		terminal.info("AcpiPM Clock Initialised... OK", "HorizonOS");

		const u8 clockInt = this->interruptAllocator.allocInt(&Clocks::timerTick, nullptr);

		this->idtManager.addEntry(clockInt, interruptTable[clockInt], Selector::KERNEL_CODE, 0, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);

		CpuManager::getCurrentCore()->coreClock.schedulerHandler.fun = &Scheduler::timerReSchedule;
		CpuManager::getCurrentCore()->coreClock.schedulerHandler.timeout = TimeUtils::msToNs(50);

		terminal.info("Clocks setup... OK", "HorizonOS");

		// Start of multicore

		Scheduler::initArch();

		CpuManager::getCurrentCore()->executionNode.init();

		for (u64 i = 0; i < moduleRequest.response->module_count; i++) {
			const limine_file *moduleFile = moduleRequest.response->modules[i];

			terminal.info("Module %u: %s Size: %u", "HorizonOS", i, moduleFile->path, moduleFile->size);

			if (stringEquals(moduleFile->string, "Symbol")) {
				terminal.info("Skipping symbol module %u: %s", "HorizonOS", i, moduleFile->path);

				continue;
			}

			if (Elf::isElf(static_cast<ElfCommonHeader *>(moduleFile->address))) {
				SimpleSpinLock lock = {};

				terminal.info("Loading module %u as ELF...", "HorizonOS", i);

				const bool hadInts = lock.lock();

				auto *moduleProcess = new Process(ProcessPriority::NORMAL, true);
				this->scheduler->addProcess(moduleProcess);

				const u64 currPageMap = Asm::readCr3();

				moduleProcess->getProcessContextKernel()->pageMap.load();

				auto *elfLocation = Elf::loadElf(static_cast<const u64 *>(moduleFile->address), moduleProcess, moduleProcess->getProcessContext(), pageSize);

				Asm::writeCr3(currPageMap);

				lock.unlock(hadInts);

				if (elfLocation != nullptr) {
					this->scheduler->addThread(true, reinterpret_cast<u64>(elfLocation), moduleProcess);
				} else {
					terminal.error("Failed to load module %u as userspace ELF: %s", "HorizonOS", i, moduleFile->path);
					this->scheduler->killProcess(moduleProcess);
				}
			}
		}

		terminal.info("Example threads registered... OK", "HorizonOS");

		// Tsc

		this->cpuManager.getBootstrapCpu()->tsc.init();
		this->cpuManager.getBootstrapCpu()->tsc.globalInit();

		terminal.info("TSC Initialised... OK", "HorizonOS");

		//Profiler::start();

		// Apic

		this->cpuManager.getBootstrapCpu()->apic.init();

		this->isInitFlag = true;

		terminal.debug("Init... OK", "HorizonOS");

		// Init uAcpi

		//this->uAcpi.init();

		//terminal.info("uACPI Initialised... OK", "HorizonOS");

		SyscallManager::init();

		terminal.info("Syscalls Initialised... OK", "HorizonOS");

		// Multithread

		this->cpuManager.startMultithread();

		this->cpuManager.getBootstrapCpu()->schedInt = this->interruptAllocator.allocInt(Scheduler::intReSchedule, nullptr);

		this->idtManager.addEntry(this->cpuManager.getBootstrapCpu()->schedInt, interruptTable[this->cpuManager.getBootstrapCpu()->schedInt], Selector::KERNEL_CODE, 0, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);

		// Todo: make one shot and restart when thread goes to sleep
		this->cpuManager.getBootstrapCpu()->apic.arm(TimeUtils::msToNs(10), clockInt, true);

		Asm::sti();

		terminal.info("All Cpus initialized...", "HorizonOS");

		// this->shutdown();

		//Profiler::stop();

		//Profiler::show("Main");

		Asm::lhlt();
	}

	void Kernel::shutdown() {
		terminal.info("Shutting down...", "HorizonOS");
	}

	auto Kernel::getGdtManager() -> GdtManager * {
		return &this->gdtManager;
	}

	auto Kernel::getTssManager() -> TssManager * {
		return &this->tssManager;
	}

	auto Kernel::getIdtManager() -> IdtManager * {
		return &this->idtManager;
	}

	auto Kernel::getIrqAllocator() -> IrqAllocator * {
		return &this->irqAllocator;
	}

	auto Kernel::getInterruptAllocator() -> InterruptAllocator * {
		return &this->interruptAllocator;
	}

	auto Kernel::getDualPic() -> DualPIC * {
		return &this->dualPic;
	}

	auto Kernel::getPIT() -> PIT * {
		return &this->pit;
	}

	auto Kernel::getKvmClock() -> KvmClock * {
		return &this->kvmClock;
	}

	auto Kernel::getHpet() -> Hpet * {
		return &this->hpet;
	}

	auto Kernel::getCpuManager() -> CpuManager * {
		return &this->cpuManager;
	}

	auto Kernel::getIOApicManager() -> IOApicManager * {
		return &this->ioApicManager;
	}

	// Multicore

	void CoreKernel::init() {
		VirtualMemoryManager::initPAT();
		
		auto *commonKernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		Terminal* terminal = CommonMain::getTerminal();

		this->coreGdtManager = GdtManager(this->coreTssManager.getTss());

		this->coreGdtManager.loadGdt();
		this->coreGdtManager.reloadRegisters();

		this->coreTssManager.allocStack();

		TssManager::updateTss();

		SyscallManager::init();

		this->coreIdtManager = commonKernel->getIdtManager();

		this->coreIdtManager->loadIdt();

		CpuManager::initSimd();

		CpuManager::getCurrentCore()->coreClock.schedulerHandler.fun = &Scheduler::timerReSchedule;
		CpuManager::getCurrentCore()->coreClock.schedulerHandler.timeout = TimeUtils::msToNs(50);

		CpuManager::getCurrentCore()->executionNode.init();

		this->cpuCore.tsc.init();

		this->cpuCore.apic.init();

		this->cpuCore.printEnabled = false;

		// TODO: schedInt prob not needed
		CpuManager::getCurrentCore()->schedInt = this->interruptAllocator.allocInt(Scheduler::intReSchedule, nullptr);
		const u8 clockInt = this->interruptAllocator.allocInt(&Clocks::timerTick, nullptr);

		this->coreIdtManager->addEntry(clockInt, interruptTable[clockInt], Selector::KERNEL_CODE, 0, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);
		this->coreIdtManager->addEntry(CpuManager::getCurrentCore()->schedInt, interruptTable[CpuManager::getCurrentCore()->schedInt], Selector::KERNEL_CODE, 0, GateDPL::KERNEL_DPL | GateType::INTERRUPT_GATE);

		this->cpuCore.apic.arm(TimeUtils::msToNs(10), clockInt, true);

		terminal->info("Core %u initialized...", "Cpu", this->cpuCore.cpuId);

		Asm::sti();

		Asm::lhlt();
	}

	auto CoreKernel::getTssManager() -> TssManager * {
		return &this->coreTssManager;
	}

	auto CoreKernel::getGdtManager() -> GdtManager * {
		return &this->coreGdtManager;
	}

	auto CoreKernel::getInterruptAllocator() -> InterruptAllocator * {
		return &this->interruptAllocator;
	}
}
