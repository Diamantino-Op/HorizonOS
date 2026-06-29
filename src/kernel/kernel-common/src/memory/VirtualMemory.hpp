#ifndef KERNEL_COMMON_VIRTUALMEMORY_HPP
#define KERNEL_COMMON_VIRTUALMEMORY_HPP

#include "Types.hpp"
#include "RBTree.hpp"

#include "PhysicalMemory.hpp"

extern char limineStart[], limineEnd[];
extern char textStart[], textEnd[];
extern char rodataStart[], rodataEnd[];
extern char dataStart[], dataEnd[];

namespace kernel::common::memory {
    constexpr u64 kernelStackSize = pageSize * 16; // 64 Kbit

    struct AllocContext;

    class PageMap {
    public:
		virtual ~PageMap() = default;

		void init(u64 *newPageTable, u64 newPhysPageTable, AllocContext *ctx, bool newIsKernel = false);

        void load() const;

        void mapPage(u64 vAddr, u64 pAddr, u8 flags, bool global, bool noExec);

        void unMapPage(u64 vAddr, bool freePage = true);

    	bool protectPage(u64 vAddr, u8 prot);

        u64 getPhysAddress(u64 vAddr) const;

        u64 *getPageTable() const;

        bool level5Paging() const;

        u64 getAddr() const;

        bool getIsKernel() const;

    	static void invPg(u64 page);

    private:
        void setPageFlags(u64 * pageAddr, u8 flags);

        u64* getOrCreatePageTable(u64* parent, u16 index, u8 flags, bool global, bool noExec);

        static RBTreeNode *allocateRBTreeNode(u64 data, u64 extraData, u64 *extraArgs);
        static void deleteRBTreeNode(RBTreeNode *node, u64 *extraArgs);

        u64* pageTable {};
        u64 physPageTable {};
        RBTree pageTree {};
        AllocContext *allocCtx {};
        bool isKernel {};
        bool isLevel5Paging = false;
    };

    class VirtualMemoryManager {
    public:
        VirtualMemoryManager() = default;
		explicit VirtualMemoryManager(u64 kernelStackTop);

        void archInit();

        u64 getVirtualKernelAddr() const;

    	static void initPAT();

    private:
        void init();

    	static u64 patEntry(u8 index, u8 type);

        u64 kernelAddrPhys {};
        u64 kernelAddrVirt {};
        u64 kernelStackTop {};
    };
}

#endif