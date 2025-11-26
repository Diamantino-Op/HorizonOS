#ifndef LIB_HOS_BASE_TIME_H
#define LIB_HOS_BASE_TIME_H

#include "Types.hpp"

class TimeUtils {
public:
	static u64 msToUs(u64 ms);

	static u64 msToNs(u64 ms);
};

#endif