#include "Msi.hpp"
#include "Pci.hpp"

#include <cstdio>
#include <vector>
#include <unistd.h>

#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"

using namespace std;

namespace {
	auto onlineCpuCount() -> uint64_t {
	    const long count = sysconf(_SC_NPROCESSORS_ONLN);

	    if (count <= 0) {
	        return 1;
	    }

	    return static_cast<uint64_t>(count);
	}

	auto allocateIntVectorForAnyCpu(uint8_t *vectorOut, uint64_t *destCpuOut, const uint64_t port) -> bool {
	    const uint64_t cpuCount = onlineCpuCount();

	    for (uint64_t cpuIndex = 0; cpuIndex < cpuCount; ++cpuIndex) {
	        if (allocIntVec(vectorOut, port, cpuIndex) == 0 and *vectorOut != 0) {
	        	*destCpuOut = cpuIndex;

	            return true;
	        }
	    }

	    return false;
	}

	auto allocateIntVectorForLapic(uint8_t *vectorOut, uint64_t lapicId, const uint64_t port) -> bool {
		return allocIntVec(vectorOut, port, lapicId, true) == 0 and *vectorOut != 0;
	}

	void freeAllocatedVector(const uint8_t vector, const uint64_t destCpu, uint64_t lapicId) {
		const bool isLapic = lapicId != 1000000;

	    freeIntVec(vector, isLapic ? lapicId : destCpu, isLapic);
	}
}

// ─── MsiManager ──────────────────────────────────────────────────────────────

auto MsiManager::instance() -> MsiManager & {
    static MsiManager inst;
    return inst;
}

auto MsiManager::allocVectors(const int count, const uint64_t notifyPort, const uint64_t lapicId) -> uint8_t {
    const scoped_lock lock(m_lock);

    if (count <= 0) {
        return 0;
    }

    vector<uint8_t> allocatedVectors;
    allocatedVectors.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        uint8_t vector = 0;
        uint64_t destCpu = lapicId != 1000000 ? lapicId : 0;

    	if ((lapicId == 1000000 and not allocateIntVectorForAnyCpu(&vector, &destCpu, notifyPort)) or (lapicId != 1000000 and not allocateIntVectorForLapic(&vector, lapicId, notifyPort))) {
    		printf("PCI/MSI: allocIntVec failed after allocating %zu vector(s)", allocatedVectors.size());
    		fflush(stdout);

    		for (const uint8_t allocatedVector : allocatedVectors) {
    			const auto allocatedIt = m_vectors.find(allocatedVector);

    			if (allocatedIt != m_vectors.end()) {
    				freeAllocatedVector(allocatedIt->second.vector, allocatedIt->second.destCpu, lapicId);
    				m_vectors.erase(allocatedIt);
    			}

    		}

    		if (!allocatedVectors.empty()) {
    			m_allocationBatches.erase(allocatedVectors.front());
    		}

    		return 0;
    	}

        AllocatedVector allocatedVector{};
        allocatedVector.vector = vector;
        allocatedVector.destCpu = destCpu;
    	allocatedVector.isLapic = lapicId != 1000000;
        allocatedVector.notifyPort = notifyPort;

        m_vectors[vector] = allocatedVector;
        allocatedVectors.push_back(vector);
    }

    m_allocationBatches[allocatedVectors.front()] = allocatedVectors;

    return allocatedVectors.front();
}

void MsiManager::freeVectors(const uint8_t base, const int count) {
    const scoped_lock lock(m_lock);

    if (count <= 0) {
        return;
    }

	const auto batchIt = m_allocationBatches.find(base);

    if (batchIt != m_allocationBatches.end()) {
        for (const uint8_t vectorValue : batchIt->second) {
            auto vectorIt = m_vectors.find(vectorValue);

            if (vectorIt == m_vectors.end()) {
                continue;
            }

            freeAllocatedVector(vectorIt->second.vector, vectorIt->second.destCpu, vectorIt->second.isLapic ? vectorIt->second.destCpu : 1000000);
            m_vectors.erase(vectorIt);
        }

        m_allocationBatches.erase(batchIt);
        return;
    }

	const auto vectorIt = m_vectors.find(base);

    if (vectorIt == m_vectors.end()) {
        return;
    }

    freeAllocatedVector(vectorIt->second.vector, vectorIt->second.destCpu, vectorIt->second.isLapic ? vectorIt->second.destCpu : 1000000);
    m_vectors.erase(vectorIt);
}

// ─── Capability walker ────────────────────────────────────────────────────────

auto pciFindCapability(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint8_t capId) -> uint8_t {
    // Capability list starts at offset 0x34 (pointer to first entry).
    // Only valid if Status register (0x06) bit 4 = Capabilities List.
    const uint16_t status = pciConfigRead16(bus, dev, func, 0x06);

    if (not static_cast<bool>(status & (1U << 4))) {
	    return 0;
    }

    uint8_t ptr = pciConfigRead8(bus, dev, func, 0x34) & 0xFCU;

    for (int depth = 0; depth < 48 && ptr >= 0x40; ++depth) {
        const uint8_t id   = pciConfigRead8(bus, dev, func, ptr);
        const uint8_t next = pciConfigRead8(bus, dev, func, ptr + 1U) & 0xFCU;

        if (id == capId) {
	        return ptr;
        }

        if (next == 0) {
	        break;
        }

        ptr = next;
    }

    return 0; // not found
}

// ─── MSI ──────────────────────────────────────────────────────────────────────

auto msiEnable(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint64_t notifyPort, const uint64_t lapicID) -> uint8_t {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSI);

    if (cap == 0) {
        printf("PCI/MSI: No MSI capability on %02x:%02x.%x", bus, dev, func);
    	fflush(stdout);

        return 0;
    }

    // Disable MSI while we configure it.
    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSI_OFF_CTRL);
    ctrl &= ~MSI_CTRL_ENABLE;
    ctrl &= ~MSI_CTRL_MME_MASK; // request 1 vector (MME=0)
    pciConfigWrite16(bus, dev, func, cap + MSI_OFF_CTRL, ctrl);

    // Allocate one vector.
    const uint8_t vector = MsiManager::instance().allocVectors(1, notifyPort, lapicID);

    if (vector == 0) {
	    return 0;
    }

    // Build address and data fields.
    const uint32_t address = MSI_ADDRESS_BASE | (static_cast<uint32_t>(lapicID) << 12);
    const uint32_t data    = MSI_DATA_EDGE_ASSERT | vector;

    const bool is64bit = (ctrl & MSI_CTRL_64BIT) != 0;

    pciConfigWrite32(bus, dev, func, cap + MSI_OFF_ADDR_LO, address);

    if (is64bit) {
        pciConfigWrite32(bus, dev, func, cap + MSI_OFF_ADDR_HI, 0);
        pciConfigWrite16(bus, dev, func, cap + MSI_OFF_DATA_64, static_cast<uint16_t>(data));
    } else {
        pciConfigWrite16(bus, dev, func, cap + MSI_OFF_DATA_32, static_cast<uint16_t>(data));
    }

    // Enable MSI.
    ctrl |= MSI_CTRL_ENABLE;
    pciConfigWrite16(bus, dev, func, cap + MSI_OFF_CTRL, ctrl);

    printf("PCI/MSI: Enabled on %02x:%02x.%x vector=0x%02x", bus, dev, func, vector);
	fflush(stdout);

    return vector;
}

void msiDisable(const uint8_t bus, const uint8_t dev, const uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSI);
	
    if (cap == 0) {
	    return;
    }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSI_OFF_CTRL);
    ctrl &= ~MSI_CTRL_ENABLE;
    pciConfigWrite16(bus, dev, func, cap + MSI_OFF_CTRL, ctrl);
}

// ─── MSI-X ───────────────────────────────────────────────────────────────────

// Returns the virtual address of the MSI-X table mapped via mmap_phys.
static auto msixTablePtr(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint8_t cap) -> volatile MsixEntry * {
    const uint32_t tableWord = pciConfigRead32(bus, dev, func, cap + MSIX_OFF_TABLE);
    const auto     bir       = static_cast<uint8_t>(tableWord & 0x7U);
    const uint32_t tableOff  = tableWord & ~0x7U;

    // BIR selects which BAR contains the table.
    const uint8_t  barReg    = 0x10U + (bir * 4U);
    const uint32_t barLo     = pciConfigRead32(bus, dev, func, barReg);

    uint64_t barPhys;

    if ((barLo & 0x6U) == 0x4U) {
        // 64-bit BAR
        const uint32_t barHi = pciConfigRead32(bus, dev, func, barReg + 4U);
        barPhys = (static_cast<uint64_t>(barHi) << 32) | (barLo & ~0xFU);
    } else {
        barPhys = barLo & ~0xFU;
    }

    const uint64_t tablePhys = barPhys + tableOff;

    // Map one page – MSI-X table entries are 16 bytes; even 2048 entries fit
    // in 32 KiB, so map 64 KiB to be safe.
    constexpr size_t MAP_SIZE = 0x10000;
    uint64_t virt = 0;

    if (mmap_phys(tablePhys, MAP_SIZE, &virt, true, MMapCacheMode::MAP_CACHE_UC) != 0) {
        printf("PCI/MSI-X: mmap_phys of table failed (phys=%llx)", static_cast<unsigned long long>(tablePhys));
    	fflush(stdout);

        return nullptr;
    }

    return reinterpret_cast<volatile MsixEntry *>(virt);
}

auto msixEnableEntry(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t tableIndex, const uint64_t notifyPort, const uint64_t lapicID) -> uint8_t {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);

    if (cap == 0) {
        printf("PCI/MSI-X: No MSI-X capability on %02x:%02x.%x", bus, dev, func);
    	fflush(stdout);

        return 0;
    }

    volatile MsixEntry *table = msixTablePtr(bus, dev, func, cap);

    if (table == nullptr) {
	    return 0;
    }

    // Mask the entry while we program it (vector ctrl bit 0).
    table[tableIndex].vectorCtrl = 1U;

    const uint8_t vector = MsiManager::instance().allocVectors(1, notifyPort, lapicID);
    if (vector == 0) { return 0; }

    const uint32_t data        = MSI_DATA_EDGE_ASSERT | vector;
	const uint32_t address     = MSI_ADDRESS_BASE | (static_cast<uint32_t>(lapicID) << 12);

    table[tableIndex].addrLo     = address;
    table[tableIndex].addrHi     = 0;
    table[tableIndex].data       = data;
    table[tableIndex].vectorCtrl = 0U; // unmask

    printf("PCI/MSI-X: Entry %u on %02x:%02x.%x vector=0x%02x", tableIndex, bus, dev, func, vector);
	fflush(stdout);

    return vector;
}

void msixDisableEntry(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t tableIndex, const uint8_t vector) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);

    if (cap == 0) {
	    return;
    }

    volatile MsixEntry *table = msixTablePtr(bus, dev, func, cap);

    if (table != nullptr) {
        table[tableIndex].vectorCtrl = 1U; // mask
    }

    MsiManager::instance().freeVectors(vector, 1);
}

void msixGlobalEnable(const uint8_t bus, const uint8_t dev, const uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);

    if (cap == 0) {
	    return;
    }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSIX_OFF_CTRL);

    ctrl &= ~MSIX_CTRL_FMASK;  // clear function mask
    ctrl |=  MSIX_CTRL_ENABLE; // enable MSI-X

    pciConfigWrite16(bus, dev, func, cap + MSIX_OFF_CTRL, ctrl);
}

void msixGlobalDisable(const uint8_t bus, const uint8_t dev, const uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);

    if (cap == 0) {
	    return;
    }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSIX_OFF_CTRL);

    ctrl &= ~MSIX_CTRL_ENABLE;

    pciConfigWrite16(bus, dev, func, cap + MSIX_OFF_CTRL, ctrl);
}