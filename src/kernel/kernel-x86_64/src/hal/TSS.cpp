#include "TSS.hpp"

#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::x86_64::hal {
	using namespace common::memory;
	using namespace common::threading;

	TssManager::TssManager() {
    	this->tssInstance = Tss();
    }

	void TssManager::allocStack() {
		this->tssInstance.rsp[0] = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize; // 16 Kb Stack

		this->tssInstance.ist[0] = reinterpret_cast<u64>(malloc(pageSize)) + pageSize; // 4 Kb Stack
		this->tssInstance.ist[1] = reinterpret_cast<u64>(malloc(pageSize)) + pageSize; // 4 Kb Stack
		this->tssInstance.ist[2] = reinterpret_cast<u64>(malloc(pageSize)) + pageSize; // 4 Kb Stack
	}

    void TssManager::updateTss() {
        updateTssAsm();
    }

    Tss *TssManager::getTss() {
        return &this->tssInstance;
    }
}