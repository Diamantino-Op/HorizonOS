#ifndef HORIZONOS_MSI_HPP
#define HORIZONOS_MSI_HPP

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

// ─── MSI address / data constants (x86-64 xAPIC/x2APIC) ──────────────────────
// TODO: Need to support multiple lapics
constexpr uint32_t MSI_ADDRESS_BASE      = 0xFEE00000U;
constexpr uint32_t MSI_ADDRESS_DEST_MASK = 0xFF000U;   // bits 19:12 = dest APIC id
constexpr uint32_t MSI_DATA_EDGE_ASSERT  = 0x0000U;    // edge-triggered, fixed delivery

// ─── PCI capability IDs ───────────────────────────────────────────────────────
constexpr uint8_t PCI_CAP_ID_MSI   = 0x05;
constexpr uint8_t PCI_CAP_ID_MSIX  = 0x11;

// ─── MSI capability register offsets (relative to cap pointer) ───────────────
constexpr uint8_t MSI_OFF_CTRL       = 0x02; // 16-bit message control
constexpr uint8_t MSI_OFF_ADDR_LO    = 0x04; // 32-bit address low
constexpr uint8_t MSI_OFF_ADDR_HI    = 0x08; // 32-bit address high (64-bit cap only)
constexpr uint8_t MSI_OFF_DATA_32    = 0x08; // data register (32-bit cap)
constexpr uint8_t MSI_OFF_DATA_64    = 0x0C; // data register (64-bit cap)

constexpr uint16_t MSI_CTRL_ENABLE   = (1U << 0);
constexpr uint16_t MSI_CTRL_64BIT    = (1U << 7);
constexpr uint16_t MSI_CTRL_MMC_MASK = (0x7U << 1); // multiple message capable
constexpr uint16_t MSI_CTRL_MME_MASK = (0x7U << 4); // multiple message enable

// ─── MSI-X capability register offsets ───────────────────────────────────────
constexpr uint8_t MSIX_OFF_CTRL     = 0x02; // 16-bit message control
constexpr uint8_t MSIX_OFF_TABLE    = 0x04; // table offset & BIR
constexpr uint8_t MSIX_OFF_PBA      = 0x08; // PBA offset & BIR

constexpr uint16_t MSIX_CTRL_ENABLE = (1U << 15);
constexpr uint16_t MSIX_CTRL_FMASK  = (1U << 14); // function mask
constexpr size_t MSIX_TABLE_MAP_SIZE = 0x10000;

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
    uint64_t destCpu;
	bool isLapic;
    uint64_t notifyPort; // port to post "irq;<vector>" when the IRQ fires
};

// ─── MSI manager (singleton) ──────────────────────────────────────────────────
class MsiManager {
public:
    static auto instance() -> MsiManager &;

    // Allocate n vectors through the HorizonOS interrupt allocator and return
    // the first vector in the batch, or 0 on failure. The allocated vectors
    // are not required to be contiguous.
    auto allocVectors(int count, uint64_t notifyPort, uint64_t lapicId = 1000000) -> uint8_t;

    // Release the allocation batch identified by base, or a single vector if
    // the batch is unknown. Vectors are freed by exact ID, not by range.
    void freeVectors(uint8_t base, int count);

private:
    MsiManager() = default;

    std::mutex                                        m_lock;
    std::unordered_map<uint8_t, AllocatedVector>      m_vectors;
    std::unordered_map<uint8_t, std::vector<uint8_t>> m_allocationBatches;
};

// ─── Capability walking helpers ───────────────────────────────────────────────
// Walk the capability-linked list; return the byte offset in config space of
// the first capability with the given id, or 0 if not found.
auto pciFindCapability(uint8_t bus, uint8_t dev, uint8_t func, uint8_t capId) -> uint8_t;

// ─── MSI programming ─────────────────────────────────────────────────────────
// Returns base vector allocated (1 vector for basic MSI), or 0 on failure.
// The capability registers are written into PCI config space directly.
auto msiEnable(uint8_t bus, uint8_t dev, uint8_t func, uint64_t notifyPort, uint64_t lapicID) -> uint8_t;
void msiDisable(uint8_t bus, uint8_t dev, uint8_t func);

// ─── MSI-X programming ───────────────────────────────────────────────────────
// tableIndex: which MSI-X table entry to program (0-based).
// Returns allocated vector, or 0 on failure.
auto msixEnableEntry(uint8_t bus, uint8_t dev, uint8_t func, uint16_t tableIndex, uint64_t notifyPort, uint64_t lapicID) -> uint8_t;
void msixDisableEntry(uint8_t bus, uint8_t dev, uint8_t func, uint16_t tableIndex, uint8_t vector);

// Enable the MSI-X capability itself (call after programming all entries).
void msixGlobalEnable(uint8_t bus, uint8_t dev, uint8_t func);
void msixGlobalDisable(uint8_t bus, uint8_t dev, uint8_t func);

#endif
