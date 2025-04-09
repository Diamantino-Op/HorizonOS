#ifndef KERNEL_COMMON_COMMONPAGING_HPP
#define KERNEL_COMMON_COMMONPAGING_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    class CommonPagingManager {
    public:
        virtual ~CommonPagingManager();

        void handlePageFault(u64 faultAddr, u8 flags);

        virtual void mapPage(uPtr* level4Page, u64 vAddr, u64 pAddr, u8 flags);

    protected:
        virtual uPtr* getOrCreatePageTable(uPtr* parent, u16 index, bool isUser);

        uPtr* currentLevel4Page {};
    };
}

#endif