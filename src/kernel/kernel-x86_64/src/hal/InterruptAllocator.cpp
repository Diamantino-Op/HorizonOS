#include "InterruptAllocator.hpp"

#include "Interrupts.hpp"
#include "Main.hpp"

namespace kernel::x86_64::hal {
	using namespace common;

	u8 InterruptAllocator::allocInt(const HandlerFun handler, u64 *ctx) {
		const bool prevIF = this->spinLock.lock();

		for (u8 i = 0; i < 224; i++) {
			if (this->handlers[i].fun == nullptr and (i + 32) != 0x80 and (i + 32) != panicStopIpiVector) {
				this->handlers[i].fun = handler;
				this->handlers[i].ctx = ctx;

				this->spinLock.unlock(prevIF);

				return i + 32;
			}
		}

		this->spinLock.unlock(prevIF);

		return 0;
	}

	bool InterruptAllocator::freeInt(const u8 intNum) {
		if (intNum < 32 or intNum > 255) {
			return false;
		}

		const bool prevIF = this->spinLock.lock();

		this->handlers[intNum - 32].fun = nullptr;
		this->handlers[intNum - 32].ctx = nullptr;

		this->spinLock.unlock(prevIF);

		return true;
	}

	bool InterruptAllocator::allocSpecific(const u8 intNum, const HandlerFun handler, u64 *ctx) {
		if (intNum < 32 or intNum > 255 or intNum == 0x80 or intNum == panicStopIpiVector) {
			return false;
		}

		const bool prevIF = this->spinLock.lock();

		this->handlers[intNum - 32].fun = handler;
		this->handlers[intNum - 32].ctx = ctx;

		this->spinLock.unlock(prevIF);

		return true;
	}

	IsrHandler *InterruptAllocator::getHandler(const u8 intNum) {
		if (intNum < 32 or intNum >= 255) {
			return nullptr;
		}

		return &handlers[intNum - 32];
	}

	u64 IrqAllocator::allocGsi(const u64 destCpu, const u16 flags, const IOApicDelivery delivery, const HandlerFun handler, u64 *ctx, const bool skipIsos) {
		const bool prevIF = this->spinLock.lock();

		for (u64 i = this->gsiBase + 1; i < this->gsiAmount + this->gsiBase; i++) {
			bool overlapsIso = false;

			if (skipIsos) {
				for (u64 j = 0; j < this->irqGsiMappingAmount; j++) {
					if (this->irqGsiMappings[j].gsi == i) {
						overlapsIso = true;

						break;
					}
				}
			}

			if (not overlapsIso) {
				if (this->usedGsis[i - this->gsiBase] == 0) {
					auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
					const CpuManager *cpuManager = kernel->getCpuManager();

					if (destCpu >= cpuManager->getCoreAmount()) {
						this->spinLock.unlock(prevIF);

						return 100000000;
					}

					const CpuCore *destCore = nullptr;

					if (destCpu == 0) {
						destCore = cpuManager->getBootstrapCpu();
					} else {
						destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
					}

					const u8 intNum = destCore->interruptAllocator->allocInt(handler, ctx);

					if (intNum == 0) {
						this->spinLock.unlock(prevIF);

						return 100000000;
					}

					kernel->getIOApicManager()->setGsi(i, intNum, destCore->apic.getId(), flags, delivery);

					this->usedGsis[i - this->gsiBase] = intNum;

					this->spinLock.unlock(prevIF);

					return i;
				}
			}
		}

		this->spinLock.unlock(prevIF);

		return 100000000;
	}

	u8 IrqAllocator::allocateIrq(const u64 irq, const u64 destCpu, const u16 flags, const IOApicDelivery delivery, const HandlerFun handler, u64 *ctx) {
		const bool prevIF = this->spinLock.lock();

		const u128 gsiRet = this->getGsi(irq);

		const auto gsi = static_cast<u64>(gsiRet);
		const auto extraFlags = static_cast<u64>(gsiRet >> 64);

		if (irq > this->gsiAmount or this->usedGsis[gsi - this->gsiBase] != 0) {
			this->spinLock.unlock(prevIF);

			return 0;
		}

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu >= cpuManager->getCoreAmount()) {
			this->spinLock.unlock(prevIF);

			return 0;
		}

		const CpuCore *destCore = nullptr;

		if (destCpu == 0) {
			destCore = cpuManager->getBootstrapCpu();
		} else {
			destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
		}

		const u8 intNum = destCore->interruptAllocator->allocInt(handler, ctx);

		if (intNum == 0) {
			this->spinLock.unlock(prevIF);

			return 0;
		}

		kernel->getIOApicManager()->setGsi(gsi, intNum, destCore->apic.getId(), extraFlags | flags, delivery);

		CommonMain::getTerminal()->debug("Allocating IRQ: %lu to gsi: %lu. Max GSIs: %lu. GSI Base: %lu", "IrqAllocator", irq, gsi, this->gsiAmount, this->gsiBase);

		this->usedGsis[gsi - this->gsiBase] = intNum;

		this->spinLock.unlock(prevIF);

		return intNum;
	}

	bool IrqAllocator::freeIrq(const u64 irq, const u64 destCpu) {
		const bool prevIF = this->spinLock.lock();

		const u64 gsi = static_cast<uint64_t>(this->getGsi(irq));

		if (this->usedGsis[gsi - this->gsiBase] == 0) {
			this->spinLock.unlock(prevIF);

			return false;
		}

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		if (destCpu >= cpuManager->getCoreAmount()) {
			this->spinLock.unlock(prevIF);

			return false;
		}

		CpuCore *destCore = nullptr;

		if (destCpu == 0) {
			destCore = cpuManager->getBootstrapCpu();
		} else {
			destCore = &cpuManager->getCoreList()[destCpu - 1].cpuCore;
		}

		if (not destCore->interruptAllocator->freeInt(this->usedGsis[gsi - this->gsiBase])) {
			this->spinLock.unlock(prevIF);

			return false;
		}

		kernel->getIOApicManager()->maskGsi(gsi);

		this->usedGsis[gsi - this->gsiBase] = 0;

		this->spinLock.unlock(prevIF);

		return true;
	}

	void IrqAllocator::setIrqGsiMappings(IrqGsiMapping *mappingsArr, const u64 amount) {
		this->irqGsiMappings = mappingsArr;
		this->irqGsiMappingAmount = amount;
	}

	void IrqAllocator::initGsiBitmap(const u64 amount) {
		this->usedGsis = new u8[amount];
		this->gsiAmount = amount;

		memset(this->usedGsis, 0, amount);
	}

	void IrqAllocator::setGsiBase(const u64 base) {
		this->gsiBase = base;
	}

	void IrqAllocator::mask(const u64 irq) const {
		const u64 gsi = static_cast<uint64_t>(this->getGsi(irq));

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());

		kernel->getIOApicManager()->maskGsi(gsi);
	}

	void IrqAllocator::unmask(const u64 irq) const {
		const u64 gsi = static_cast<uint64_t>(this->getGsi(irq));

		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());

		kernel->getIOApicManager()->unmaskGsi(gsi);
	}

	u128 IrqAllocator::getGsi(const u64 irq) const {
		for (u64 i = 0; i < this->irqGsiMappingAmount; i++) {
			if (this->irqGsiMappings[i].irq == irq) {
				return (static_cast<u128>(this->irqGsiMappings[i].flags) << 64) | this->irqGsiMappings[i].gsi;
			}
		}

		return irq;
	}
}
