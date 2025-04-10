#ifndef KERNEL_COMMON_VIRTUALMEMORY_HPP
#define KERNEL_COMMON_VIRTUALMEMORY_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;
    constexpr u64 hhdmOffsetLevel4 = 0xFFFF800000000000;
    constexpr u64 hhdmOffsetLevel5 = 0xFFFF800000000000;

    template <class T> class PagingManager {
    public:
      PagingManager();
      ~PagingManager() = default;

      void handlePageFault(u64 faultAddr, u8 flags);

      void mapPage(u64 vAddr, u64 pAddr, u8 flags);

      void unMapPage(u64 vAddr);

    protected:
      void setPageFlags(uPtr * pageAddr, u8 flags);

      void loadPageTable();

      uPtr* getOrCreatePageTable(T* parent, u16 index, u8 flags);

      T currentLevel4Page {};
    };
}

#endif