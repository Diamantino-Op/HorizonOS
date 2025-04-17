#ifndef KERNEL_COMMON_VIRTUALMEMORY_HPP
#define KERNEL_COMMON_VIRTUALMEMORY_HPP

#include "Types.hpp"

#include "PhysicalMemory.hpp"

extern char limineStart[], limineEnd[];
extern char textStart[], textEnd[];
extern char rodataStart[], rodataEnd[];
extern char dataStart[], dataEnd[];

namespace kernel::common::memory {
    constexpr u64 kernelMemorySize = pageSize * 1024 * 4; // 16 Mbit

    class VirtualMemoryManager {
    public:
        VirtualMemoryManager() = default;
        VirtualMemoryManager(u64 kernelStackTop, u64 *mainPtr);

        void archInit();

        void handlePageFault(u64 faultAddr, u8 flags);

        void mapPage(u64 vAddr, u64 pAddr, u8 flags, bool noExec);

        void unMapPage(u64 vAddr) const;

        u64 getPhysAddress(u64 vAddr) const;

    protected:
        void init();

        void setPageFlags(uPtr * pageAddr, u8 flags);

        void loadPageTable() const;

        uPtr* getOrCreatePageTable(uPtr* parent, u16 index, u8 flags, bool noExec);

        u64 *mainPtr;

        uPtr* currentMainPage {};
        u64 currentHhdm {};
        u64 kernelAddrPhys {};
        u64 kernelAddrVirt {};
        u64 kernelStackTop {};
        bool isLevel5Paging = false;
    };
}

#endif