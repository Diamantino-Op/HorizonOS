#include "IDAllocator.hpp"

namespace kernel::common::threading {
	u16 PIDAllocator::freePIDs[maxProcesses];
	i32 PIDAllocator::pidTop = -1;
	TicketSpinLock PIDAllocator::lock {};

	void PIDAllocator::init() {
		const bool prevIF = lock.lock();
		pidTop = -1;

		for (int i = maxProcesses - 1; i >= 0; --i) {
			freePIDs[++pidTop] = i;
		}

		lock.unlock(prevIF);
	}

	u16 PIDAllocator::allocPID() {
		const bool prevIF = lock.lock();

		if (pidTop < 0) {
			lock.unlock(prevIF);
			return -1;
		}

		const u16 pid = freePIDs[pidTop--];
		lock.unlock(prevIF);

		return pid;
	}

	void PIDAllocator::freePID(const u16 pid) {
		const bool prevIF = lock.lock();

		if (pid >= maxProcesses || pidTop >= maxProcesses - 1) {
			lock.unlock(prevIF);
			return;
		}

		freePIDs[++pidTop] = pid;
		lock.unlock(prevIF);
	}

	PRIDAllocator::PRIDAllocator() {
		this->init();
	}

	void PRIDAllocator::init() {
		const bool prevIF = this->lock.lock();
		this->pridTop = -1;

		for (int i = maxProcThreads - 1; i >= 0; --i) {
			this->freePRIDs[++this->pridTop] = i;
		}

		this->lock.unlock(prevIF);
	}

	u8 PRIDAllocator::allocPRID() {
		const bool prevIF = this->lock.lock();

		if (this->pridTop < 0) {
			this->lock.unlock(prevIF);
			return -1;
		}

		const u8 prid = this->freePRIDs[this->pridTop--];
		this->lock.unlock(prevIF);

		return prid;
	}

	void PRIDAllocator::freePRID(const u8 prid) {
		const bool prevIF = this->lock.lock();

		if (prid >= maxProcThreads || this->pridTop >= maxProcThreads - 1) {
			this->lock.unlock(prevIF);
			return;
		}

		this->freePRIDs[++this->pridTop] = prid;
		this->lock.unlock(prevIF);
	}

	u16 TIDAllocator::freeTIDs[maxThreads];
	i32 TIDAllocator::tidTop = -1;
	TicketSpinLock TIDAllocator::lock {};

	void TIDAllocator::init() {
		const bool prevIF = lock.lock();
		tidTop = -1;

		for (int i = maxThreads - 1; i >= 0; --i) {
			freeTIDs[++tidTop] = i;
		}

		lock.unlock(prevIF);
	}

	u16 TIDAllocator::allocTID() {
		const bool prevIF = lock.lock();

		if (tidTop < 0) {
			lock.unlock(prevIF);
			return -1;
		}

		const u16 tid = freeTIDs[tidTop--];
		lock.unlock(prevIF);

		return tid;
	}

	void TIDAllocator::freeTID(const u16 tid) {
		const bool prevIF = lock.lock();

		if (tid >= maxThreads || tidTop >= maxThreads - 1) {
			lock.unlock(prevIF);
			return;
		}

		freeTIDs[++tidTop] = tid;
		lock.unlock(prevIF);
	}
}
