#include "TSS.hpp"

namespace x86_64::hal {
    void TssManager::initTss() {
        this->tssInstance = Tss();

        this->tssInstance.rsp[0] = (usize)&this->tssStack + sizeof(this->tssStack);
    }

    /*void TssManager::updateTss() {
        asm volatile(
            "movw %0, %%ax;"
            "ltr %%ax;"
            "ret;"
            :
            : "r" (0x28)
            : "%ax"
        );
    }*/

    Tss TssManager::getTss() {
        return this->tssInstance;
    }
}