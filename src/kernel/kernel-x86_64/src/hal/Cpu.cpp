#define LIMINE_API_REVISION 3

#include "Cpu.hpp"

#include "CommonMain.hpp"
#include "utils/CpuId.hpp"
#include "utils/Asm.hpp"

#include "limine.h"

extern limine_mp_request mpRequest;

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	void CpuManager::init() {
		if (mpRequest.response != nullptr) {
			Terminal* terminal = CommonMain::getTerminal();

			this->coreAmount = mpRequest.response->cpu_count;
			this->cpuList = new CpuCore[this->coreAmount];

			for (u64 i = 0; i < this->coreAmount; i++) {
				this->cpuList[i].apic.setId(mpRequest.response->cpus[i]->lapic_id);
				this->cpuList[i].cpuId = mpRequest.response->cpus[i]->processor_id;

				if (this->cpuList[i].apic.getId() == mpRequest.response->bsp_lapic_id) {
					this->bootstrapApic = &this->cpuList[i].apic;
				} else {
					mpRequest.response->cpus[i]->extra_argument = reinterpret_cast<u64>(&this->cpuList[i]);
					mpRequest.response->cpus[i]->goto_address = [](limine_mp_info * info) {
						coreInit(reinterpret_cast<CpuCore *>(info->extra_argument));
					};
				}
			}

			this->brand = CpuId::getBrand();
			this->vendor = CpuId::getVendor();

			this->hasX2Apic = mpRequest.response->flags & 0x1;

			terminal->info("Brand: %.48s", "Cpu", this->brand);
			terminal->info("Vendor: %.12s", "Cpu", this->vendor);

			terminal->info("Cores: %u", "Cpu", this->coreAmount);

			terminal->debug("Features:", "Cpu");
			terminal->debug("	X2Apic: %u", "Cpu", this->hasX2Apic);
			terminal->debug("	XSave: %u", "Cpu", CpuId::hasXSave());
			terminal->debug("	XSave Size: %u", "Cpu", CpuId::getXSaveSize());
			terminal->debug("	Avx: %u", "Cpu", CpuId::hasAvx());
			terminal->debug("	Avx 512: %u", "Cpu", CpuId::hasAvx512());

			this->initSimd();
		}
	}

	void CpuManager::initSimd() const {
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

		terminal->info("SIMD Enabled", "Cpu");
	}

	void coreInit(const Cpu *cpu) {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->info("Core %u initialized...", "Cpu", cpu->cpuId);

		for (;;) {
			Asm::hlt();
		}
	}
}