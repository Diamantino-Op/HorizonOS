#include "TSS.hpp"

namespace kernel::x86_64::hal {
	TssManager::TssManager() {
    	this->tssInstance = Tss();

    	//this->tssInstance.rsp[0] = (usize)&this->kernelStack + sizeof(this->kernelStack);
    }

    void TssManager::updateTss() {
        updateTssAsm();
    }

    Tss TssManager::getTss() {
        return this->tssInstance;
    }
}