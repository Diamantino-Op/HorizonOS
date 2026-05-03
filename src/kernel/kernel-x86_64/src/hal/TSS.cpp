#include "TSS.hpp"

#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::x86_64::hal {
	using namespace common::memory;
	using namespace common::threading;

	void TssManager::allocStack() {
		this->tssPtrs.rsp[0] = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize; // 16 Kb Stack

		this->tssPtrs.ist[0] = reinterpret_cast<u64>(this->generalIntStack + sizeof(this->generalIntStack)); // 2 Kb Stack
		this->tssPtrs.ist[1] = reinterpret_cast<u64>(this->nmiIntStack + sizeof(this->nmiIntStack)); // 2 Kb Stack
		this->tssPtrs.ist[2] = reinterpret_cast<u64>(this->exceptionIntStack + sizeof(this->exceptionIntStack)); // 2 Kb Stack
		this->tssPtrs.ist[3] = reinterpret_cast<u64>(this->syscallStack + sizeof(this->syscallStack)); // 2 Kb Stack

		this->tssTemp.rsp[0] = this->tssPtrs.rsp[0];

		this->tssTemp.ist[0] = this->tssPtrs.ist[0];
		this->tssTemp.ist[1] = this->tssPtrs.ist[1];
		this->tssTemp.ist[2] = this->tssPtrs.ist[2];
		this->tssTemp.ist[3] = this->tssPtrs.ist[3];
	}

    void TssManager::updateTss() {
        updateTssAsm();
    }

	TssTemp *TssManager::getTssTemp() {
		return &this->tssTemp;
	}

	TssPtrs *TssManager::getTssPtrs() {
		return &this->tssPtrs;
	}
}