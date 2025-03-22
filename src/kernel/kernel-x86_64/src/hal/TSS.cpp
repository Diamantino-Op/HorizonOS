#include "TSS.hpp"

namespace hal::x86_64 {
    void TssManager::initTss() {
        this->tssInstance = Tss();

        this->tssInstance.rsp[0] = (usize)&this->kernelStack + sizeof(this->kernelStack);
    }

    void TssManager::updateTss() {
        asm volatile(
            "mov %0, %%ax;"
            "ltr %%ax;"
            "ret;"
            :
            : "r" (0x28)
            : "%ax"
        );
    }

    Tss TssManager::getTss() {
        return this->tssInstance;
    }
}