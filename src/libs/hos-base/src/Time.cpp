#include "Time.hpp"

u64 TimeUtils::msToUs(const u64 ms) {
	return ms * 1'000'000ull;
}

u64 TimeUtils::msToNs(const u64 ms) {
	return ms * 1'000ull;
}