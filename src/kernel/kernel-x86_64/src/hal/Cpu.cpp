#include "Cpu.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "utils/CpuId.hpp"
#include "utils/Asm.hpp"

extern limine_mp_request mpRequest;

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	void CpuManager::init() {
		if (mpRequest.response != nullptr) {
			Terminal* terminal = CommonMain::getTerminal();

			this->coreAmount = mpRequest.response->cpu_count;

			this->brand = CpuId::getBrand();
			this->vendor = CpuId::getVendor();

			this->hasX2Apic = static_cast<bool>(mpRequest.response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC);

			terminal->info("Brand: %.48s", "Cpu", this->brand);
			terminal->info("Vendor: %.12s", "Cpu", this->vendor);

			terminal->info("Cores: %u", "Cpu", this->coreAmount);

			terminal->debug("Features:", "Cpu");
			terminal->debug("	X2Apic: %u", "Cpu", static_cast<u8>(this->hasX2Apic));
			terminal->debug("	XSave: %u", "Cpu", static_cast<u8>(CpuId::hasXSave()));
			terminal->debug("	XSave Size: %u", "Cpu", CpuId::getXSaveSize());
			terminal->debug("	Avx: %u", "Cpu", static_cast<u8>(CpuId::hasAvx()));
			terminal->debug("	Avx 512: %u", "Cpu", static_cast<u8>(CpuId::hasAvx512()));
		}
	}

	auto CpuManager::getCoreAmount() const -> u64 {
		return this->coreAmount;
	}

	auto CpuManager::getCoreList() const -> CoreKernel * {
		return this->cpuList;
	}

	auto CpuManager::getBootstrapCpu() const -> CpuCore * {
		return this->bootstrapCpu;
	}

	void CpuManager::initSimd() {
		Terminal* terminal = CommonMain::getTerminal();

		Cr0RegisterU cr0Val {};
		cr0Val.value = Asm::readCr0();

		cr0Val.reg.emulation = 0;
		cr0Val.reg.taskSwitched = 0;
		cr0Val.reg.monitorCoProcessor = 1;
		cr0Val.reg.numericError = 1;

		Asm::writeCr0(cr0Val.value);

		Cr4RegisterU cr4Val {};
		cr4Val.value = Asm::readCr4();

		cr4Val.reg.fxsaveFxrstorSupport = 1;
		cr4Val.reg.unmaskedSimdExceptionsSupport = 1;

		Asm::writeCr4(cr4Val.value);

		u64 xCr0 = 0;

		if (CpuId::hasXSave()) {
			cr4Val.reg.xsaveExtendedEnable = 1;

			Asm::writeCr4(cr4Val.value);

			XCr0RegisterU xCr0Val {};

			xCr0Val.reg.xsaveSaveX87 = 1;
			xCr0Val.reg.xsaveSaveSSE = 1;

			if (CpuId::hasAvx()) {
				xCr0Val.reg.avxEnable = 1;
			}

			if (CpuId::hasAvx512()) {
				xCr0Val.reg.avx512Enable = 1;
				xCr0Val.reg.zmm0_15Enable = 1;
				xCr0Val.reg.zmm16_31Enable = 1;
			}

			Asm::writeXCr(0, xCr0Val.value);
			xCr0 = Asm::readXCr(0);
		}

		Asm::fninit();

		terminal->info("SIMD Enabled on CPU %u: CR0=0x%.16lx CR4=0x%.16lx XCR0=0x%.16lx", "Cpu", CpuManager::getCurrentCore()->cpuId, Asm::readCr0(), Asm::readCr4(), xCr0);
	}

	void CpuManager::initSimdContext(const uPtr *ptr) {
		Asm::fninit();

		saveSimdContext(ptr);
	}

	void CpuManager::saveSimdContext(const uPtr *ptr) {
		saveSimdContextChecked(ptr, nullptr, 0);
	}

	void CpuManager::saveSimdContextChecked(const uPtr *ptr, const uPtr *originalPtr, const u64 allocSize) {
		const u64 addr = reinterpret_cast<u64>(ptr);
		const u64 original = reinterpret_cast<u64>(originalPtr);

		const bool outsideAlloc = originalPtr != nullptr && (allocSize < 512 || addr < original || addr - original > allocSize - 512);

		if (ptr == nullptr || (addr & 0x3fU) != 0 || outsideAlloc) {
			auto *term = CommonMain::getTerminal();

			term->printfBoth(true, "\033[0;31mSIMD save panic: invalid save area");
			term->printfBoth(true, "\033[0;31m  ptr=0x%.16lx original=0x%.16lx size=%lu cpu=%lu KGS=0x%.16lx",
				addr, original, allocSize, CpuManager::getCurrentCore()->cpuId, Asm::rdmsr(KGSBAS));

			for (;;) {
				Asm::cli();
				Asm::hlt();
			}
		}

		if (CpuId::hasXSave()) {
			Asm::xsave(ptr);
		} else {
			Asm::fxsave(ptr);
		}
	}

	void CpuManager::loadSimdContext(const uPtr *ptr) {
		loadSimdContextChecked(ptr, nullptr, 0);
	}

	void CpuManager::loadSimdContextChecked(const uPtr *ptr, const uPtr *originalPtr, const u64 allocSize) {
		const u64 addr = reinterpret_cast<u64>(ptr);
		const u64 original = reinterpret_cast<u64>(originalPtr);

		const bool outsideAlloc = originalPtr != nullptr && (allocSize < 512 || addr < original || addr - original > allocSize - 512);

		if (ptr == nullptr || (addr & 0x3fU) != 0 || outsideAlloc) {
			auto *term = CommonMain::getTerminal();

			term->printfBoth(true, "\033[0;31mSIMD restore panic: invalid save area");
			term->printfBoth(true, "\033[0;31m  ptr=0x%.16lx original=0x%.16lx size=%lu cpu=%lu KGS=0x%.16lx",
				addr, original, allocSize, CpuManager::getCurrentCore()->cpuId, Asm::rdmsr(KGSBAS));

			for (;;) {
				Asm::cli();
				Asm::hlt();
			}
		}

		sanitizeSimdContext(ptr);

		if (CpuId::hasXSave()) {
			Asm::xrstor(ptr);
		} else {
			Asm::fxrstor(ptr);
		}
	}

	void CpuManager::sanitizeSimdContext(const uPtr *ptr) {
		if (ptr == nullptr) {
			return;
		}

		auto *bytes = reinterpret_cast<u8 *>(const_cast<uPtr *>(ptr));
		auto *mxcsr = reinterpret_cast<u32 *>(bytes + 24);
		auto *mxcsrMask = reinterpret_cast<u32 *>(bytes + 28);
		const u32 validMask = *mxcsrMask != 0 ? *mxcsrMask : 0x0000FFBFU;

		*mxcsr &= validMask;
	}

	void CpuManager::startBootCore() {
		Terminal* terminal = CommonMain::getTerminal();

		this->bootstrapCpu = new CpuCore();

		for (u64 i = 0; i < this->coreAmount; i++) {
			if (mpRequest.response->cpus[i]->lapic_id == mpRequest.response->bsp_lapic_id) {
				this->bootstrapCpu->apic.setId(mpRequest.response->cpus[i]->lapic_id);
				this->bootstrapCpu->apic.setIsX2Apic(this->hasX2Apic);
				this->bootstrapCpu->cpuArrId = 0;
				this->bootstrapCpu->cpuId = mpRequest.response->cpus[i]->processor_id;
				this->bootstrapCpu->lapicId = mpRequest.response->cpus[i]->lapic_id;
				this->bootstrapCpu->tssManager = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getTssManager();
				this->bootstrapCpu->gdtManager = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getGdtManager();
				this->bootstrapCpu->interruptAllocator = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getInterruptAllocator();

				setCorePointer(this->bootstrapCpu);

				terminal->debug("BSP Cpu: %u", "Cpu", mpRequest.response->cpus[i]->processor_id);

				break;
			}
		}
	}

	void CpuManager::startMultithread() {
		if (this->coreAmount == 1) {
			return;
		}

		this->cpuList = new CoreKernel[this->coreAmount - 1];

		u64 j = 0;

		for (u64 i = 0; i < this->coreAmount; i++) {
			if (mpRequest.response->cpus[i]->lapic_id != mpRequest.response->bsp_lapic_id) {
				this->cpuList[j].cpuCore.apic.setId(mpRequest.response->cpus[i]->lapic_id);
				this->cpuList[j].cpuCore.apic.setIsX2Apic(this->hasX2Apic);
				this->cpuList[j].cpuCore.cpuArrId = j + 1;
				this->cpuList[j].cpuCore.cpuId = mpRequest.response->cpus[i]->processor_id;
				this->cpuList[j].cpuCore.lapicId = mpRequest.response->cpus[i]->lapic_id;
				this->cpuList[j].cpuCore.tssManager = this->cpuList[j].getTssManager();
				this->cpuList[j].cpuCore.gdtManager = this->cpuList[j].getGdtManager();
				this->cpuList[j].cpuCore.interruptAllocator = this->cpuList[j].getInterruptAllocator();

				this->initCore(i, j);

				++j;
			}
		}

		while (__atomic_load_n(&this->initializedApplicationCores, __ATOMIC_ACQUIRE) < this->coreAmount - 1) {
			Asm::pause();
		}
	}

	void CpuManager::notifyApplicationCoreInitialized() {
		__atomic_fetch_add(&this->initializedApplicationCores, 1, __ATOMIC_RELEASE);
	}

	void CpuManager::setCorePointer(CpuCore *core) {
		const u64 corePtr = reinterpret_cast<u64>(core);

		// Keep core pointer in KGSBAS (persists through swapgs).
		// UGSBAS will hold either core ptr (kernel after user entry) or user GS (user mode).
		Asm::wrmsr(KGSBAS, corePtr);
	}

	auto CpuManager::getCurrentCore() -> CpuCore * {
		return reinterpret_cast<CpuCore *>(Asm::rdmsr(KGSBAS));
	}

	void CpuManager::initCore(const u64 coreId, const u64 listIndex) const {
		mpRequest.response->cpus[coreId]->extra_argument = reinterpret_cast<u64>(&this->cpuList[listIndex]);
		mpRequest.response->cpus[coreId]->goto_address = reinterpret_cast<limine_goto_address>(&bootCore);
	}

	void bootCore(const limine_mp_info *info) {
		CommonMain::getInstance()->getKernelAllocContextHHDM()->pageMap.load();

		auto *coreKernel = reinterpret_cast<CoreKernel *>(info->extra_argument);

		CpuManager::setCorePointer(&coreKernel->cpuCore);

		//asm volatile("swapgs" ::: "memory");

		coreKernel->init();
	}
}
