#ifndef KERNEL_COMMON_LIMINEHELPER_HPP
#define KERNEL_COMMON_LIMINEHELPER_HPP

#include "Types.hpp"

namespace kernel::common::utils {
	bool isPhysicalAddressInLimineModuleResponse(u64 physAddress);
	bool isPhysicalAddressInLimineMpResponse(u64 physAddress);
	bool isPhysicalAddressInLimineModuleOrMpResponse(u64 physAddress);

	bool doesPhysicalRangeOverlapLimineModuleResponse(u64 physAddress, u64 size);
	bool doesPhysicalRangeOverlapLimineMpResponse(u64 physAddress, u64 size);
	bool doesPhysicalRangeOverlapLimineModuleOrMpResponse(u64 physAddress, u64 size);
}

#endif
