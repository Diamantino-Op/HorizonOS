#include "Msi.hpp"
#include "Pci.hpp"

#include <cstdio>
#include <string>
#include <array>

#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"

using namespace std;

// ─── IRQ dispatch shim ───────────────────────────────────────────────────────
// One C-linkage shim per vector is expensive — instead we use a single
// generic handler that receives the vector number in ctx.

struct IrqCtx {
    uint8_t vector;
};

static uint32_t irqShim(void *ctx) {
    const auto *c = static_cast<IrqCtx *>(ctx);
    MsiManager::instance().dispatch(c->vector);

	return 0;
}

// ─── MsiManager ──────────────────────────────────────────────────────────────

MsiManager &MsiManager::instance() {
    static MsiManager inst;
    return inst;
}

uint8_t MsiManager::allocVectors(int count, int notifyPort) {
    scoped_lock lock(m_lock);

    if (m_nextVector + static_cast<uint8_t>(count) > MSI_VECTOR_MAX) {
        printf("PCI/MSI: Vector space exhausted\n");

        return 0;
    }

    const uint8_t base = m_nextVector;

    for (int i = 0; i < count; ++i) {
        const uint8_t v = base + static_cast<uint8_t>(i);

        auto *ctx   = new IrqCtx{v};
    	void *handle = nullptr;

    	// TODO: Needs checking
    	// TODO: Fix
        /*if (install_irq_handler(v, irqShim, ctx, handle) != 0) {
            printf("PCI/MSI: install_irq_handler failed for vector %u\n", v);

            delete ctx;

            // Roll back already-installed vectors.
            for (int j = 0; j < i; ++j) {
                const uint8_t rv = base + static_cast<uint8_t>(j);
                auto it = m_vectors.find(rv);

                if (it != m_vectors.end()) {
                	// TODO: Fix
                	//uninstall_irq_handler(irqShim, it->second.irqHandle);

                    delete ctx; // already freed above for failed

                    m_vectors.erase(it);
                }
            }
            return 0;
        }*/

        m_vectors[v] = AllocatedVector {.vector = v, .irqHandle = handle, .notifyPort = notifyPort};
    }

    m_nextVector = base + static_cast<uint8_t>(count);
    return base;
}

void MsiManager::freeVectors(uint8_t base, int count) {
    scoped_lock lock(m_lock);

    for (int i = 0; i < count; ++i) {
        const uint8_t v = base + static_cast<uint8_t>(i);
        auto it = m_vectors.find(v);
        if (it == m_vectors.end()) { continue; }

    	// TODO: Fix
        //uninstall_irq_handler(irqShim, it->second.irqHandle);
        m_vectors.erase(it);
    }
}

// TODO
void MsiManager::dispatch(uint8_t vector) {
    // Called from kernel IRQ context — keep it short.
    scoped_lock lock(m_lock);

    auto it = m_vectors.find(vector);
    if (it == m_vectors.end()) { return; }

    const int port = it->second.notifyPort;
    if (port <= 0) { return; }

    // Post "irq;<vector>" to the driver service.
    auto *msg   = new hos_msg();
    string body = "irq;" + to_string(vector);

    msg->port   = port;
    msg->buffer = static_cast<void *>(body.data());
    msg->length = body.size();

    send_horizonos_message(pciPort, port, msg);
    delete msg;
}

// ─── Capability walker ────────────────────────────────────────────────────────

uint8_t pciFindCapability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t capId) {
    // Capability list starts at offset 0x34 (pointer to first entry).
    // Only valid if Status register (0x06) bit 4 = Capabilities List.
    const uint16_t status = pciConfigRead16(bus, dev, func, 0x06);
    if (!(status & (1u << 4))) { return 0; }

    uint8_t ptr = pciConfigRead8(bus, dev, func, 0x34) & 0xFCu;

    for (int depth = 0; depth < 48 && ptr >= 0x40; ++depth) {
        const uint8_t id   = pciConfigRead8(bus, dev, func, ptr);
        const uint8_t next = pciConfigRead8(bus, dev, func, ptr + 1u) & 0xFCu;

        if (id == capId) { return ptr; }
        if (next == 0)   { break; }
        ptr = next;
    }

    return 0; // not found
}

// ─── MSI ──────────────────────────────────────────────────────────────────────

uint8_t msiEnable(uint8_t bus, uint8_t dev, uint8_t func, int notifyPort) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSI);

    if (cap == 0) {
        printf("PCI/MSI: No MSI capability on %02x:%02x.%x\n", bus, dev, func);
        return 0;
    }

    // Disable MSI while we configure it.
    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSI_OFF_CTRL);
    ctrl &= ~MSI_CTRL_ENABLE;
    ctrl &= ~MSI_CTRL_MME_MASK; // request 1 vector (MME=0)
    pciConfigWrite16(bus, dev, func, cap + MSI_OFF_CTRL, ctrl);

    // Allocate one vector.
    const uint8_t vector = MsiManager::instance().allocVectors(1, notifyPort);
    if (vector == 0) { return 0; }

    // Build address and data fields.
    const uint32_t address = MSI_ADDRESS_BASE; // dest = APIC id 0
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

    printf("PCI/MSI: Enabled on %02x:%02x.%x vector=0x%02x\n",
           bus, dev, func, vector);

    return vector;
}

void msiDisable(uint8_t bus, uint8_t dev, uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSI);
    if (cap == 0) { return; }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSI_OFF_CTRL);
    ctrl &= ~MSI_CTRL_ENABLE;
    pciConfigWrite16(bus, dev, func, cap + MSI_OFF_CTRL, ctrl);
}

// ─── MSI-X ───────────────────────────────────────────────────────────────────

// Returns the virtual address of the MSI-X table mapped via mmap_phys.
static volatile MsixEntry *msixTablePtr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t cap) {
    const uint32_t tableWord = pciConfigRead32(bus, dev, func, cap + MSIX_OFF_TABLE);
    const uint8_t  bir       = static_cast<uint8_t>(tableWord & 0x7u);
    const uint32_t tableOff  = tableWord & ~0x7u;

    // BIR selects which BAR contains the table.
    const uint8_t  barReg    = 0x10u + bir * 4u;
    const uint32_t barLo     = pciConfigRead32(bus, dev, func, barReg);

    uint64_t barPhys;

    if ((barLo & 0x6u) == 0x4u) {
        // 64-bit BAR
        const uint32_t barHi = pciConfigRead32(bus, dev, func, barReg + 4u);
        barPhys = (static_cast<uint64_t>(barHi) << 32) | (barLo & ~0xFu);
    } else {
        barPhys = barLo & ~0xFu;
    }

    const uint64_t tablePhys = barPhys + tableOff;

    // Map one page – MSI-X table entries are 16 bytes; even 2048 entries fit
    // in 32 KiB, so map 64 KiB to be safe.
    constexpr size_t MAP_SIZE = 0x10000;
    uint64_t virt = 0;

    if (mmap_phys(tablePhys, MAP_SIZE, &virt) != 0) {
        printf("PCI/MSI-X: mmap_phys of table failed (phys=%llx)\n",
               static_cast<unsigned long long>(tablePhys));
        return nullptr;
    }

    return reinterpret_cast<volatile MsixEntry *>(virt);
}

uint8_t msixEnableEntry(uint8_t bus, uint8_t dev, uint8_t func,
                        uint16_t tableIndex, int notifyPort) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);

    if (cap == 0) {
        printf("PCI/MSI-X: No MSI-X capability on %02x:%02x.%x\n", bus, dev, func);
        return 0;
    }

    volatile MsixEntry *table = msixTablePtr(bus, dev, func, cap);
    if (!table) { return 0; }

    // Mask the entry while we program it (vector ctrl bit 0).
    table[tableIndex].vectorCtrl = 1u;

    const uint8_t vector = MsiManager::instance().allocVectors(1, notifyPort);
    if (vector == 0) { return 0; }

    const uint32_t data    = MSI_DATA_EDGE_ASSERT | vector;
    const uint32_t address = MSI_ADDRESS_BASE;

    table[tableIndex].addrLo     = address;
    table[tableIndex].addrHi     = 0;
    table[tableIndex].data       = data;
    table[tableIndex].vectorCtrl = 0u; // unmask

    printf("PCI/MSI-X: Entry %u on %02x:%02x.%x vector=0x%02x\n",
           tableIndex, bus, dev, func, vector);

    return vector;
}

void msixDisableEntry(uint8_t bus, uint8_t dev, uint8_t func,
                      uint16_t tableIndex, uint8_t vector) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);
    if (cap == 0) { return; }

    volatile MsixEntry *table = msixTablePtr(bus, dev, func, cap);
    if (table) {
        table[tableIndex].vectorCtrl = 1u; // mask
    }

    MsiManager::instance().freeVectors(vector, 1);
}

void msixGlobalEnable(uint8_t bus, uint8_t dev, uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);
    if (cap == 0) { return; }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSIX_OFF_CTRL);
    ctrl &= ~MSIX_CTRL_FMASK;  // clear function mask
    ctrl |=  MSIX_CTRL_ENABLE; // enable MSI-X
    pciConfigWrite16(bus, dev, func, cap + MSIX_OFF_CTRL, ctrl);
}

void msixGlobalDisable(uint8_t bus, uint8_t dev, uint8_t func) {
    const uint8_t cap = pciFindCapability(bus, dev, func, PCI_CAP_ID_MSIX);
    if (cap == 0) { return; }

    uint16_t ctrl = pciConfigRead16(bus, dev, func, cap + MSIX_OFF_CTRL);
    ctrl &= ~MSIX_CTRL_ENABLE;
    pciConfigWrite16(bus, dev, func, cap + MSIX_OFF_CTRL, ctrl);
}