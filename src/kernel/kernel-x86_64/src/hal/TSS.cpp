#include "TSS.hpp"

namespace x86_64::hal {
    void TssManager::initTss() {
        this->tssInstance = Tss();

        this->tssInstance.rsp[0] = (usize)&this->kernelStack + sizeof(this->kernelStack);
    }

    void TssManager::updateTss() {
        updateTssAsm();
    }

    Tss TssManager::getTss() {
        return this->tssInstance;
    }
}