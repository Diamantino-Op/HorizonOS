#include "TSS.hpp"

#include "memory/MainMemory.hpp"

#include "memory/PhysicalMemory.hpp"

namespace kernel::x86_64::hal {
	using namespace common::memory;

	constexpr u64 stackSize = pageSize * 4;

	TssManager::TssManager() {
    	this->tssInstance = Tss();
    }

	void TssManager::allocStack() {
		this->tssInstance.rsp[0] = reinterpret_cast<u64>(malloc(stackSize)) + stackSize; // 64 Kb Stack
	}

    void TssManager::updateTss() {
        updateTssAsm();
    }

    Tss TssManager::getTss() const {
        return this->tssInstance;
    }
}