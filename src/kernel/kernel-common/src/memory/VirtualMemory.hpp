#ifndef KERNEL_COMMON_VIRTUALMEMORY_HPP
#define KERNEL_COMMON_VIRTUALMEMORY_HPP

#include "Types.hpp"

#include "PhysicalMemory.hpp"

extern char limineStart[], limineEnd[];
extern char textStart[], textEnd[];
extern char rodataStart[], rodataEnd[];
extern char dataStart[], dataEnd[];

namespace kernel::common::memory {
    constexpr u64 kernelStackSize = pageSize * 16; // 64 Kbit

    class PageMap {
    public:
        void init(u64 *pageTable);

        void load();

        void mapPage(u64 vAddr, u64 pAddr, u8 flags, bool noExec);

        void unMapPage(u64 vAddr) const;

        u64 getPhysAddress(u64 vAddr) const;

        u64 *getPageTable() const;

    private:
        void setPageFlags(u64 * pageAddr, u8 flags);

        u64* getOrCreatePageTable(u64* parent, u16 index, u8 flags, bool noExec);

        u64* pageTable {};
        bool isLevel5Paging = false;
    };

    class VirtualMemoryManager {
    public:
        VirtualMemoryManager() = default;
		explicit VirtualMemoryManager(u64 kernelStackTop);

        void archInit();

    private:
        void init();

        u64 kernelAddrPhys {};
        u64 kernelAddrVirt {};
        u64 kernelStackTop {};
    };
}

#endif