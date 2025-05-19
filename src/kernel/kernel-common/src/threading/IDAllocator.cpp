#include "IDAllocator.hpp"

namespace kernel::common::threading {
	u16 PIDAllocator::freePIDs[maxProcesses];
	i32 PIDAllocator::pidTop = -1;

	void PIDAllocator::init() {
		for (int i = maxProcesses - 1; i >= 0; --i) {
			freePIDs[++pidTop] = i;
		}
	}

	u16 PIDAllocator::allocPID() {
		if (pidTop < 0) {
			return -1;
		}

		return freePIDs[pidTop--];
	}

	void PIDAllocator::freePID(const u16 pid) {
		if (pid >= maxProcesses) {
			return;
		}

		freePIDs[++pidTop] = pid;
	}

	u16 TIDAllocator::freeTIDs[maxThreads];
	i32 TIDAllocator::tidTop = -1;

	void TIDAllocator::init() {
		for (int i = maxThreads - 1; i >= 0; --i) {
			freeTIDs[++tidTop] = i;
		}
	}

	u16 TIDAllocator::allocTID() {
		if (tidTop < 0) {
			return -1;
		}

		return freeTIDs[tidTop--];
	}

	void TIDAllocator::freeTID(const u16 tid) {
		if (tid >= maxThreads) {
			return;
		}

		freeTIDs[++tidTop] = tid;
	}
}