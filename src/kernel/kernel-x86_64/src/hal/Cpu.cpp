#include "Cpu.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "utils/CpuId.hpp"
#include "utils/Asm.hpp"
#include "Math.hpp"

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

			this->hasX2Apic = mpRequest.response->flags & LIMINE_MP_X2APIC;

			terminal->info("Brand: %.48s", "Cpu", this->brand);
			terminal->info("Vendor: %.12s", "Cpu", this->vendor);

			terminal->info("Cores: %u", "Cpu", this->coreAmount);

			terminal->debug("Features:", "Cpu");
			terminal->debug("	X2Apic: %u", "Cpu", this->hasX2Apic);
			terminal->debug("	XSave: %u", "Cpu", CpuId::hasXSave());
			terminal->debug("	XSave Size: %u", "Cpu", CpuId::getXSaveSize());
			terminal->debug("	Avx: %u", "Cpu", CpuId::hasAvx());
			terminal->debug("	Avx 512: %u", "Cpu", CpuId::hasAvx512());
		}
	}

	u64 CpuManager::getCoreAmount() const {
		return this->coreAmount;
	}

	CoreKernel *CpuManager::getCoreList() const {
		return this->cpuList;
	}

	CpuCore *CpuManager::getBootstrapCpu() const {
		return this->bootstrapCpu;
	}

	void CpuManager::initSimd() {
		Terminal* terminal = CommonMain::getTerminal();

		Cr0RegisterU cr0Val {};
		cr0Val.value = Asm::readCr0();

		cr0Val.reg.emulation = 0;
		cr0Val.reg.monitorCoProcessor = 1;
		cr0Val.reg.numericError = 1;

		Asm::writeCr0(cr0Val.value);

		Cr4RegisterU cr4Val {};
		cr4Val.value = Asm::readCr4();

		cr4Val.reg.fxsaveFxrstorSupport = 1;
		cr4Val.reg.unmaskedSimdExceptionsSupport = 1;

		Asm::writeCr4(cr4Val.value);

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
		}

		Asm::fninit();

		terminal->info("SIMD Enabled", "Cpu");
	}

	void CpuManager::initSimdContext(const uPtr *ptr) {
		Asm::fninit();

		saveSimdContext(ptr);
	}

	void CpuManager::saveSimdContext(const uPtr *ptr) {
		if (CpuId::hasXSave()) {
			Asm::xsave(ptr);
		} else {
			Asm::fxsave(ptr);
		}
	}

	void CpuManager::loadSimdContext(const uPtr *ptr) {
		if (CpuId::hasXSave()) {
			Asm::xrstor(ptr);
		} else {
			Asm::fxrstor(ptr);
		}
	}

	void CpuManager::startMultithread() {
		Terminal* terminal = CommonMain::getTerminal();

		this->bootstrapCpu = new CpuCore();
		this->cpuList = new CoreKernel[this->coreAmount - 1];

		u64 j = 0;

		for (u64 i = 0; i < this->coreAmount; i++) {
			if (mpRequest.response->cpus[i]->lapic_id == mpRequest.response->bsp_lapic_id) {
				this->bootstrapCpu->apic.setId(mpRequest.response->cpus[i]->lapic_id);
				this->bootstrapCpu->apic.setIsX2Apic(this->hasX2Apic);
				this->bootstrapCpu->cpuId = mpRequest.response->cpus[i]->processor_id;

				setCorePointer(this->bootstrapCpu);

				terminal->debug("BSP Cpu: %u", "Cpu", mpRequest.response->cpus[i]->processor_id);
			} else {
				this->cpuList[j].cpuCore.apic.setId(mpRequest.response->cpus[i]->lapic_id);
				this->cpuList[j].cpuCore.apic.setIsX2Apic(this->hasX2Apic);
				this->cpuList[j].cpuCore.cpuId = mpRequest.response->cpus[i]->processor_id;

				this->initCore(i, j);

				++j;
			}
		}
	}

	void CpuManager::setCorePointer(CpuCore *core) {
		Asm::wrmsr(UGSBAS, reinterpret_cast<u64>(core));
	}

	CpuCore *CpuManager::getCurrentCore() {
		return reinterpret_cast<CpuCore *>(Asm::rdmsr(UGSBAS));
	}

	void CpuManager::initCore(const u64 coreId, const u64 listIndex) const {
		mpRequest.response->cpus[coreId]->extra_argument = reinterpret_cast<u64>(&this->cpuList[listIndex]);
		mpRequest.response->cpus[coreId]->goto_address = reinterpret_cast<limine_goto_address>(&bootCore);
	}

	void bootCore(const limine_mp_info *info) {
		CommonMain::getInstance()->getKernelAllocContext()->pageMap.load();

		const auto coreKernel = reinterpret_cast<CoreKernel *>(info->extra_argument);

		CpuManager::setCorePointer(&coreKernel->cpuCore);

		coreKernel->init();
	}
}