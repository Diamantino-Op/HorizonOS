#include "MainMemory.hpp"

#include "VirtualAllocator.hpp"
#include "CommonMain.hpp"

namespace kernel::common::memory {
	void *memcpy(void *destAddr, const void *srcAddr, usize size) {
		const auto pDest = static_cast<u8 *>(destAddr);
		const auto pSrc = static_cast<const u8 *>(srcAddr);

		for (usize i = 0; i < size; i++) {
			pDest[i] = pSrc[i];
		}

		return destAddr;
	}

	void *memset(void *addr, const int val, const usize size) {
		const auto p = static_cast<u8 *>(addr);

		for (usize i = 0; i < size; i++) {
			p[i] = static_cast<u8>(val);
		}

		return addr;
	}

	void *memmove(void *destAddr, const void *srcAddr, const usize size) {
		const auto pDest = static_cast<u8 *>(destAddr);
		const auto pSrc = static_cast<const u8 *>(srcAddr);

		if (srcAddr > destAddr) {
			for (usize i = 0; i < size; i++) {
				pDest[i] = pSrc[i];
			}
		} else if (srcAddr < destAddr) {
			for (usize i = size; i > 0; i--) {
				pDest[i-1] = pSrc[i-1];
			}
		}

		return destAddr;
	}

	int memcmp(const void *addr1, const void *addr2, const usize size) {
		const auto p1 = static_cast<const u8 *>(addr1);
		const auto p2 = static_cast<const u8 *>(addr2);

		for (usize i = 0; i < size; i++) {
			if (p1[i] != p2[i]) {
				return p1[i] < p2[i] ? -1 : 1;
			}
		}

		return 0;
	}

	// TODO: Make it get the right context for threads
	void* malloc(const usize size) {
		AllocContext *ctx = CommonMain::getInstance()->getKernelAllocContext();
		return VirtualAllocator::alloc(ctx, size);
	}

	void free(void* ptr) {
		AllocContext *ctx = CommonMain::getInstance()->getKernelAllocContext();
		return VirtualAllocator::free(ctx, static_cast<u64 *>(ptr));
	}
}

using namespace kernel::common::memory;

void* operator new(const usize size) {
	return malloc(size);
}

void* operator new[](const usize size) {
	return malloc(size);
}

void operator delete(void* ptr) noexcept {
	free(ptr);
}

void operator delete(void* ptr, usize) noexcept {
	free(ptr);
}

void operator delete[](void* ptr) noexcept {
	free(ptr);
}

void operator delete[](void* ptr, usize) noexcept {
	free(ptr);
}