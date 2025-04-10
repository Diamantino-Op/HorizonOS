#ifndef KERNEL_COMMON_COMMONPAGING_HPP
#define KERNEL_COMMON_COMMONPAGING_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;

    class PagingManager {
    public:
        PagingManager() = default;
        ~PagingManager() = default;

        void handlePageFault(u64 faultAddr, u8 flags);

        void mapPage(uPtr* level4Page, u64 vAddr, u64 pAddr, u8 flags);

    protected:
        void loadPageTable();

        uPtr* getOrCreatePageTable(uPtr* parent, u16 index, bool isUser);

        uPtr* currentLevel4Page {};
    };
}

#endif