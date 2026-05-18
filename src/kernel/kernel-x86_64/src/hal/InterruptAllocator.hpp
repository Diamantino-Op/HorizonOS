#ifndef KERNEL_X86_64_INTERRUPTALLOCATOR_HPP
#define KERNEL_X86_64_INTERRUPTALLOCATOR_HPP

#include "Apic.hpp"
#include "SpinLock.hpp"
#include "Types.hpp"

namespace kernel::x86_64::hal {
	using HandlerFun = u32(*)(u64 *ctx);

	struct IsrHandler {
		HandlerFun fun {};
		u64 *ctx {};
	};

	struct IrqGsiMapping {
		u64 irq {};
		u64 gsi {};
		u16 flags {};
	};

	class InterruptAllocator {
	public:
		u8 allocInt(HandlerFun handler, u64 *ctx);
		bool freeInt(u8 intNum);

		bool allocSpecific(u8 intNum, HandlerFun handler, u64 *ctx);

		IsrHandler *getHandler(u8 intNum);

	private:
		TicketSpinLock spinLock {};

		IsrHandler handlers[224] {};
	};

	class IrqAllocator {
	public:
		u64 allocGsi(u64 destCpu, u16 flags, IOApicDelivery delivery, HandlerFun handler, u64 *ctx, bool skipIsos = true);
		u8 allocateIrq(u64 irq, u64 destCpu, u16 flags, IOApicDelivery delivery, HandlerFun handler, u64 *ctx);
		bool freeIrq(u64 irq, u64 destCpu);

		void setIrqGsiMappings(IrqGsiMapping *mappingsArr, u64 amount);

		void initGsiBitmap(u64 amount);
		void setGsiBase(u64 base);

		void mask(u64 irq) const;
		void unmask(u64 irq) const;

	private:
		u128 getGsi(u64 irq) const;

		TicketSpinLock spinLock {};

		IrqGsiMapping *irqGsiMappings {};
		u64 irqGsiMappingAmount {};

		u8 *usedGsis {};
		u64 gsiAmount {};
		u64 gsiBase {};
	};
}

#endif
