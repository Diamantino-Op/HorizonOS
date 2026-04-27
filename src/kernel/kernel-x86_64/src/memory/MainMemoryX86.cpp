#include "memory/MainMemory.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::memory {
	using namespace x86_64::utils;

	u32 gErmsState = 0; // 0=unknown, 1=no ERMS, 2=ERMS

	bool hasErmsFast() {
		auto state = __atomic_load_n(&gErmsState, __ATOMIC_ACQUIRE);

		if (state == 0) {
			state = CpuId::hasERMS() ? 2U : 1U;
			__atomic_store_n(&gErmsState, state, __ATOMIC_RELEASE);
		}

		return state == 2U;
	}

	void *memcpy(void *destAddr, const void *srcAddr, usize size) {
		if (destAddr == srcAddr || size == 0) {
			return destAddr;
		}

		auto pDest = static_cast<u8 *>(destAddr);
		auto pSrc = static_cast<const u8 *>(srcAddr);

		if (hasErmsFast()) {
			auto copyCount = size;

			asm volatile(
				"cld\n\t"
				"rep movsb"
				: "+D"(pDest), "+S"(pSrc), "+c"(copyCount)
				:
				: "memory", "cc"
			);

			return destAddr;
		}

		usize qwordCount = size >> 3;
		usize byteCount = size & 0x7;

		asm volatile(
			"cld\n\t"
			"rep movsq"
			: "+D"(pDest), "+S"(pSrc), "+c"(qwordCount)
			:
			: "memory", "cc"
		);

		asm volatile(
			"rep movsb"
			: "+D"(pDest), "+S"(pSrc), "+c"(byteCount)
			:
			: "memory", "cc"
		);

		return destAddr;
	}

	void *memset(void *addr, const int val, const usize size) {
		if (size == 0) {
			return addr;
		}

		auto p = static_cast<u8 *>(addr);

		if (hasErmsFast()) {
			u8 byteValue = static_cast<u8>(val);
			auto byteCount = size;

			asm volatile(
				"cld\n\t"
				"rep stosb"
				: "+D"(p), "+c"(byteCount), "+a"(byteValue)
				:
				: "memory", "cc"
			);

			return addr;
		}

		const u64 byte = static_cast<u8>(val);
		const u64 qwordPattern = byte * 0x0101010101010101ULL;

		usize qwordCount = size >> 3;
		usize byteCount = size & 0x7;
		u64 qwordValue = qwordPattern;
		u8 byteValue = static_cast<u8>(val);

		asm volatile(
			"cld\n\t"
			"rep stosq"
			: "+D"(p), "+c"(qwordCount), "+a"(qwordValue)
			:
			: "memory", "cc"
		);

		asm volatile(
			"rep stosb"
			: "+D"(p), "+c"(byteCount), "+a"(byteValue)
			:
			: "memory", "cc"
		);

		return addr;
	}

	void *memmove(void *destAddr, const void *srcAddr, const usize size) {
		if (destAddr == srcAddr || size == 0) {
			return destAddr;
		}

		auto pDest = static_cast<u8 *>(destAddr);
		auto pSrc = static_cast<const u8 *>(srcAddr);

		const auto destPos = reinterpret_cast<usize>(pDest);
		const auto srcPos = reinterpret_cast<usize>(pSrc);

		if (destPos < srcPos || destPos >= (srcPos + size)) {
			return memcpy(destAddr, srcAddr, size);
		} else {
			auto pDestEnd = pDest + (size - 1);
			auto pSrcEnd = pSrc + (size - 1);
			auto copyCount = size;

			asm volatile(
				"std\n\t"
				"rep movsb\n\t"
				"cld"
				: "+D"(pDestEnd), "+S"(pSrcEnd), "+c"(copyCount)
				:
				: "memory", "cc"
			);
		}

		return destAddr;
	}
}