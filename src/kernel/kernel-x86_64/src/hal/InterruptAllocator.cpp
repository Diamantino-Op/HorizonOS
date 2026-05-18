#include "InterruptAllocator.hpp"

#include "Main.hpp"

namespace kernel::x86_64::hal {
	using namespace common;

	u8 InterruptAllocator::allocInt(const HandlerFun handler, u64 *ctx) {
		for (u8 i = 0; i < 224; i++) {
			if (this->handlers[i].fun == nullptr and (i + 32) != 0x80) {
				this->handlers[i].fun = handler;
				this->handlers[i].ctx = ctx;

				return i + 32;
			}
		}

		return 0;
	}

	bool InterruptAllocator::freeInt(const u8 intNum) {
		if (intNum - 32 < 0 or intNum - 32 > 223) {
			return false;
		}

		this->handlers[intNum - 32].fun = nullptr;
		this->handlers[intNum - 32].ctx = nullptr;

		return true;
	}

	bool InterruptAllocator::allocSpecific(u8 intNum, HandlerFun handler, u64 *ctx) {
		if (intNum - 32 < 0 or intNum - 32 > 223) {
			return false;
		}

		this->handlers[intNum].fun = handler;
		this->handlers[intNum].ctx = ctx;

		return true;
	}

	IsrHandler *InterruptAllocator::getHandler(const u8 intNum) {
		if (intNum - 32 < 0 or intNum - 32 > 223) {
			return nullptr;
		}

		return &handlers[intNum - 32];
	}

	u64 IrqAllocator::allocGsi(const u64 destCpu, const u16 flags, const IOApicDelivery delivery, const HandlerFun handler, u64 *ctx, const bool skipIsos) {
		for (u64 i = this->gsiBase; i < this->gsiAmount + this->gsiBase; i++) {
			bool overlapsIso = false;

			if (skipIsos) {
				for (u64 j = 0; j < this->irqGsiMappingAmount; j++) {
					if (this->irqGsiMappings[i].gsi == i) {
						overlapsIso = true;

						break;
					}
				}
			}

			if (not overlapsIso) {
				if (this->usedGsis[i - this->gsiBase] == 0) {
					auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
					const CpuManager *cpuManager = kernel->getCpuManager();

					if (destCpu > cpuManager->getCoreAmount()) {
						return 0;
					}

					CpuCore *destCore = nullptr;

					if (destCpu == 0) {
						destCore = cpuManager->getBootstrapCpu();
					} else {
						destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
					}

					const u8 intNum = destCore->interruptAllocator->allocInt(handler, ctx);

					if (intNum == 0) {
						return 0;
					}

					kernel->getIOApicManager()->setGsi(i, intNum, destCore->apic.getId(), flags, delivery);

					this->usedGsis[i - this->gsiBase] = intNum;

					return i;
				}
			}
		}

		return 0;
	}

	u8 IrqAllocator::allocateIrq(const u64 irq, const u64 destCpu, const u16 flags, const IOApicDelivery delivery, const HandlerFun handler, u64 *ctx) {
		const u64 gsi = this->getGsi(irq);

		if (irq > this->gsiAmount or this->usedGsis[gsi - this->gsiBase] != 0) {
			return 0;
		}

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu > cpuManager->getCoreAmount()) {
			return 0;
		}

		CpuCore *destCore = nullptr;

		if (destCpu == 0) {
			destCore = cpuManager->getBootstrapCpu();
		} else {
			destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
		}

		const u8 intNum = destCore->interruptAllocator->allocInt(handler, ctx);

		if (intNum == 0) {
			return 0;
		}

		kernel->getIOApicManager()->setGsi(gsi, intNum, destCore->apic.getId(), flags, delivery);

		this->usedGsis[gsi - this->gsiBase] = intNum;

		return intNum;
	}

	bool IrqAllocator::freeIrq(const u64 irq, const u64 destCpu) {
		const u64 gsi = this->getGsi(irq);

		if (this->usedGsis[gsi - this->gsiBase] == 0) {
			return false;
		}

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu > cpuManager->getCoreAmount()) {
			return false;
		}

		CpuCore *destCore = nullptr;

		if (destCpu == 0) {
			destCore = cpuManager->getBootstrapCpu();
		} else {
			destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
		}

		if (not destCore->interruptAllocator->freeInt(this->usedGsis[gsi - this->gsiBase])) {
			return false;
		}

		kernel->getIOApicManager()->maskGsi(gsi);

		this->usedGsis[gsi - this->gsiBase] = 0;

		return true;
	}

	void IrqAllocator::setIrqGsiMappings(IrqGsiMapping *mappingsArr, const u64 amount) {
		this->irqGsiMappings = mappingsArr;
		this->irqGsiMappingAmount = amount;
	}

	void IrqAllocator::initGsiBitmap(const u64 amount) {
		this->usedGsis = new u8[amount];
	}

	void IrqAllocator::setGsiBase(const u64 base) {
		this->gsiBase = base;
	}

	void IrqAllocator::mask(const u64 irq) const {
		const u64 gsi = this->getGsi(irq);

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());

		kernel->getIOApicManager()->maskGsi(gsi);
	}

	void IrqAllocator::unmask(const u64 irq) const {
		const u64 gsi = this->getGsi(irq);

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());

		kernel->getIOApicManager()->unmaskGsi(gsi);
	}

	u64 IrqAllocator::getGsi(const u64 irq) const {
		for (u64 i = 0; i < this->irqGsiMappingAmount; i++) {
			if (this->irqGsiMappings[i].irq == irq) {
				return this->irqGsiMappings[i].gsi;
			}
		}

		return irq;
	}
}