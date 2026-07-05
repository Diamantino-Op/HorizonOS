#include "LimineHelper.hpp"

#include "CommonMain.hpp"

#include "limine.h"

extern limine_module_request moduleRequest;
extern limine_mp_request mpRequest;

namespace kernel::common::utils {
	namespace {
		bool addressInRange(const u64 address, const u64 base, const u64 size) {
			return size != 0 and address >= base and address - base < size;
		}

		u64 rangeEnd(const u64 base, const u64 size) {
			if (base > ~0ULL - size) {
				return ~0ULL;
			}

			return base + size;
		}

		bool rangesOverlap(const u64 firstBase, const u64 firstSize, const u64 secondBase, const u64 secondSize) {
			if (firstSize == 0 or secondSize == 0) {
				return false;
			}

			return firstBase < rangeEnd(secondBase, secondSize) and secondBase < rangeEnd(firstBase, firstSize);
		}

		u64 liminePtrToPhys(const void *ptr) {
			const u64 address = reinterpret_cast<u64>(ptr);
			const u64 hhdm = kernel::common::CommonMain::getCurrentHhdm();

			if (hhdm != 0 and address >= hhdm) {
				return address - hhdm;
			}

			return address;
		}

		bool physInLimineObject(const u64 physAddress, const void *ptr, const u64 size) {
			if (ptr == nullptr) {
				return false;
			}

			return addressInRange(physAddress, liminePtrToPhys(ptr), size);
		}

		bool physRangeOverlapsLimineObject(const u64 physAddress, const u64 rangeSize, const void *ptr, const u64 objectSize) {
			if (ptr == nullptr) {
				return false;
			}

			return rangesOverlap(physAddress, rangeSize, liminePtrToPhys(ptr), objectSize);
		}

		u64 stringSize(const char *str) {
			u64 size = 0;

			while (str[size] != '\0') {
				size++;
			}

			return size + 1;
		}

		bool physInLimineString(const u64 physAddress, const char *str) {
			if (str == nullptr) {
				return false;
			}

			return physInLimineObject(physAddress, str, stringSize(str));
		}

		bool physRangeOverlapsLimineString(const u64 physAddress, const u64 rangeSize, const char *str) {
			if (str == nullptr) {
				return false;
			}

			return physRangeOverlapsLimineObject(physAddress, rangeSize, str, stringSize(str));
		}
	}

	bool isPhysicalAddressInLimineModuleResponse(const u64 physAddress) {
		const limine_module_response *response = moduleRequest.response;

		if (response == nullptr) {
			return false;
		}

		if (physInLimineObject(physAddress, response, sizeof(limine_module_response))) {
			return true;
		}

		if (physInLimineObject(physAddress, response->modules, response->module_count * sizeof(limine_file *))) {
			return true;
		}

		if (response->modules == nullptr) {
			return false;
		}

		for (u64 i = 0; i < response->module_count; i++) {
			const limine_file *module = response->modules[i];

			if (physInLimineObject(physAddress, module, sizeof(limine_file))) {
				return true;
			}

			if (module != nullptr and physInLimineString(physAddress, module->path)) {
				return true;
			}

			if (module != nullptr and physInLimineString(physAddress, module->string)) {
				return true;
			}

			if (module != nullptr and physInLimineObject(physAddress, module->address, module->size)) {
				return true;
			}
		}

		return false;
	}

	bool isPhysicalAddressInLimineMpResponse(const u64 physAddress) {
		const limine_mp_response *response = mpRequest.response;

		if (response == nullptr) {
			return false;
		}

		if (physInLimineObject(physAddress, response, sizeof(limine_mp_response))) {
			return true;
		}

		if (physInLimineObject(physAddress, response->cpus, response->cpu_count * sizeof(limine_mp_info *))) {
			return true;
		}

		if (response->cpus == nullptr) {
			return false;
		}

		for (u64 i = 0; i < response->cpu_count; i++) {
			if (physInLimineObject(physAddress, response->cpus[i], sizeof(limine_mp_info))) {
				return true;
			}
		}

		return false;
	}

	bool isPhysicalAddressInLimineModuleOrMpResponse(const u64 physAddress) {
		return isPhysicalAddressInLimineModuleResponse(physAddress) or isPhysicalAddressInLimineMpResponse(physAddress);
	}

	bool doesPhysicalRangeOverlapLimineModuleResponse(const u64 physAddress, const u64 size) {
		const limine_module_response *response = moduleRequest.response;

		if (response == nullptr) {
			return false;
		}

		if (physRangeOverlapsLimineObject(physAddress, size, response, sizeof(limine_module_response))) {
			return true;
		}

		if (physRangeOverlapsLimineObject(physAddress, size, response->modules, response->module_count * sizeof(limine_file *))) {
			return true;
		}

		if (response->modules == nullptr) {
			return false;
		}

		for (u64 i = 0; i < response->module_count; i++) {
			const limine_file *module = response->modules[i];

			if (physRangeOverlapsLimineObject(physAddress, size, module, sizeof(limine_file))) {
				return true;
			}

			if (module != nullptr and physRangeOverlapsLimineString(physAddress, size, module->path)) {
				return true;
			}

			if (module != nullptr and physRangeOverlapsLimineString(physAddress, size, module->string)) {
				return true;
			}

			if (module != nullptr and physRangeOverlapsLimineObject(physAddress, size, module->address, module->size)) {
				return true;
			}
		}

		return false;
	}

	bool doesPhysicalRangeOverlapLimineMpResponse(const u64 physAddress, const u64 size) {
		const limine_mp_response *response = mpRequest.response;

		if (response == nullptr) {
			return false;
		}

		if (physRangeOverlapsLimineObject(physAddress, size, response, sizeof(limine_mp_response))) {
			return true;
		}

		if (physRangeOverlapsLimineObject(physAddress, size, response->cpus, response->cpu_count * sizeof(limine_mp_info *))) {
			return true;
		}

		if (response->cpus == nullptr) {
			return false;
		}

		for (u64 i = 0; i < response->cpu_count; i++) {
			if (physRangeOverlapsLimineObject(physAddress, size, response->cpus[i], sizeof(limine_mp_info))) {
				return true;
			}
		}

		return false;
	}

	bool doesPhysicalRangeOverlapLimineModuleOrMpResponse(const u64 physAddress, const u64 size) {
		return doesPhysicalRangeOverlapLimineModuleResponse(physAddress, size) or doesPhysicalRangeOverlapLimineMpResponse(physAddress, size);
	}
}
