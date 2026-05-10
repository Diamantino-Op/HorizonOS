#ifndef HORIZONOS_MSI_HPP
#define HORIZONOS_MSI_HPP

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>

// ─── IRQ vector allocation ────────────────────────────────────────────────────
// Vectors 0x00-0x2F are reserved (CPU exceptions + legacy PIC).
// We hand out vectors from 0x30 upward.
constexpr uint8_t MSI_VECTOR_BASE = 0x30;
constexpr uint8_t MSI_VECTOR_MAX  = 0xFE; // 0xFF is spurious

// ─── MSI address / data constants (x86-64 xAPIC/x2APIC) ──────────────────────
// Destination field uses BSP APIC id 0 for simplicity; NVMe/USB don't care.
constexpr uint32_t MSI_ADDRESS_BASE      = 0xFEE00000u;
constexpr uint32_t MSI_ADDRESS_DEST_MASK = 0xFF000u;   // bits 19:12 = dest APIC id
constexpr uint32_t MSI_DATA_EDGE_ASSERT  = 0x0000u;    // edge-triggered, fixed delivery

// ─── PCI capability IDs ───────────────────────────────────────────────────────
constexpr uint8_t PCI_CAP_ID_MSI   = 0x05;
constexpr uint8_t PCI_CAP_ID_MSIX  = 0x11;

// ─── MSI capability register offsets (relative to cap pointer) ───────────────
constexpr uint8_t MSI_OFF_CTRL      = 0x02; // 16-bit message control
constexpr uint8_t MSI_OFF_ADDR_LO   = 0x04; // 32-bit address low
constexpr uint8_t MSI_OFF_ADDR_HI   = 0x08; // 32-bit address high (64-bit cap only)
constexpr uint8_t MSI_OFF_DATA_32   = 0x08; // data register (32-bit cap)
constexpr uint8_t MSI_OFF_DATA_64   = 0x0C; // data register (64-bit cap)

constexpr uint16_t MSI_CTRL_ENABLE  = (1u << 0);
constexpr uint16_t MSI_CTRL_64BIT   = (1u << 7);
constexpr uint16_t MSI_CTRL_MMC_MASK = (0x7u << 1); // multiple message capable
constexpr uint16_t MSI_CTRL_MME_MASK = (0x7u << 4); // multiple message enable

// ─── MSI-X capability register offsets ───────────────────────────────────────
constexpr uint8_t MSIX_OFF_CTRL     = 0x02; // 16-bit message control
constexpr uint8_t MSIX_OFF_TABLE    = 0x04; // table offset & BIR
constexpr uint8_t MSIX_OFF_PBA     = 0x08; // PBA offset & BIR

constexpr uint16_t MSIX_CTRL_ENABLE = (1u << 15);
constexpr uint16_t MSIX_CTRL_FMASK  = (1u << 14); // function mask

// ─── MSI-X table entry (16 bytes) ────────────────────────────────────────────
struct MsixEntry {
    uint32_t addrLo;
    uint32_t addrHi;
    uint32_t data;
    uint32_t vectorCtrl; // bit 0 = masked
};
static_assert(sizeof(MsixEntry) == 16, "MsixEntry must be 16 bytes");

// ─── Allocated vector descriptor ─────────────────────────────────────────────
struct AllocatedVector {
    uint8_t  vector;
    void    *irqHandle;  // handle returned by install_irq_handler
    int      notifyPort; // port to post "irq;<vector>" when the IRQ fires
};

// ─── MSI manager (singleton) ──────────────────────────────────────────────────
class MsiManager {
public:
    static MsiManager &instance();

    // Allocate n contiguous vectors, install kernel handler, return base vector
    // or 0 on failure.  notifyPort: the port to wake when the IRQ fires.
    uint8_t allocVectors(int count, int notifyPort);

    // Release vector(s) starting at base for count entries.
    void freeVectors(uint8_t base, int count);

    // Called by the kernel IRQ shim – post a message to the registered port.
    void dispatch(uint8_t vector);

private:
    MsiManager() = default;

    std::mutex                                m_lock;
    uint8_t                                   m_nextVector{MSI_VECTOR_BASE};
    std::unordered_map<uint8_t, AllocatedVector> m_vectors;
};

// ─── Capability walking helpers ───────────────────────────────────────────────
// Walk the capability linked list; return the byte offset in config space of
// the first capability with the given id, or 0 if not found.
uint8_t pciFindCapability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t capId);

// ─── MSI programming ─────────────────────────────────────────────────────────
// Returns base vector allocated (1 vector for basic MSI), or 0 on failure.
// The capability registers are written into PCI config space directly.
uint8_t msiEnable(uint8_t bus, uint8_t dev, uint8_t func, int notifyPort);
void    msiDisable(uint8_t bus, uint8_t dev, uint8_t func);

// ─── MSI-X programming ───────────────────────────────────────────────────────
// tableIndex: which MSI-X table entry to program (0-based).
// Returns allocated vector, or 0 on failure.
uint8_t msixEnableEntry(uint8_t bus, uint8_t dev, uint8_t func,
                        uint16_t tableIndex, int notifyPort);
void    msixDisableEntry(uint8_t bus, uint8_t dev, uint8_t func,
                         uint16_t tableIndex, uint8_t vector);
// Enable the MSI-X capability itself (call after programming all entries).
void    msixGlobalEnable(uint8_t bus, uint8_t dev, uint8_t func);
void    msixGlobalDisable(uint8_t bus, uint8_t dev, uint8_t func);

#endif