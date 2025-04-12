#ifndef KERNEL_COMMON_VIRTUALMEMORY_HPP
#define KERNEL_COMMON_VIRTUALMEMORY_HPP

#include "Types.hpp"

#include "limine.h"

__attribute__((used, section(".limine_requests")))
static volatile limine_hhdm_request hhdmRequest = {
  .id = LIMINE_HHDM_REQUEST,
  .revision = 0,
  .response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_kernel_address_request kernelAddressRequest = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = nullptr,
};

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;

    extern "C" u8 limineStart[], limineEnd[];
    extern "C" u8 textStart[], textEnd[];
    extern "C" u8 rodataStart[], rodataEnd[];
    extern "C" u8 dataStart[], dataEnd[];

    template <class T> class VirtualMemoryManager {
    public:
        VirtualMemoryManager();

        void init();

        void handlePageFault(u64 faultAddr, u8 flags);

        void mapPage(u64 vAddr, u64 pAddr, u8 flags);

        void unMapPage(u64 vAddr);

    protected:
        void setPageFlags(uPtr * pageAddr, u8 flags);

        void loadPageTable();

        uPtr* getOrCreatePageTable(T* parent, u16 index, u8 flags);

        T currentMainPage {};
        u64 currentHhdm {};
        u64 kernelAddrPhys {};
        u64 kernelAddrVirt {};
        bool isLevel5Paging = false;
    };
}

#endif