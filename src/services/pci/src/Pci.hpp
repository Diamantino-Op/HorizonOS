#ifndef HORIZONOS_PCI_HPP
#define HORIZONOS_PCI_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <sys/mman.h>

// ─── Port assignments ─────────────────────────────────────────────────────────
constexpr int REGISTRY_PORT = 1;

extern uint64_t pciPort;
extern uint64_t uacpiPort;

// ─── Legacy PCI port I/O base addresses ──────────────────────────────────────
constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA    = 0xCFC;

// ─── PCIe ECAM segment descriptor ────────────────────────────────────────────
struct McfgSegment {
    uint64_t  baseAddress;   // Physical base of MMIO window
    uint16_t  segmentGroup;
    uint8_t   startBus;
    uint8_t   endBus;
    void     *mappedBase;    // Virtual address after mmap_phys (or nullptr)
    size_t    mappedSize;    // Size passed to mmap_phys
};

// ─── PCI device descriptor ───────────────────────────────────────────────────
struct PciDevice {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendorId;
    uint16_t deviceId;
    uint8_t  classCode;
    uint8_t  subclass;
    uint8_t  progIf;
    uint8_t  headerType;
    bool     isPcie;         // true if accessed via ECAM
};

enum class PciBridgeType {
	None,
	HostBridge,       // 06:00
	IsaBridge,        // 06:01
	PciToPciBridge,   // 06:04  — introduces a secondary bus
	OtherBridge,      // 06:xx
};

// ─── ECAM segment table (populated from MCFG) ────────────────────────────────
// Protected by the same ioperm that the message loop holds.
extern std::vector<McfgSegment> g_ecamSegments;
extern std::mutex               g_ecamMutex;

// ─── ECAM helper: returns the mapped pointer for a BDF, or nullptr ────────────
void *ecamDeviceBase(uint8_t bus, uint8_t dev, uint8_t func);

// Register an ECAM segment parsed from MCFG (maps it via mmap_phys).
bool addEcamSegment(uint64_t physBase, uint16_t seg, uint8_t startBus, uint8_t endBus);

// ─── Config-space accessors (ECAM preferred, legacy fallback) ─────────────────
uint32_t pciConfigRead32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
uint16_t pciConfigRead16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
uint8_t  pciConfigRead8 (uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);

void pciConfigWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t value);
void pciConfigWrite16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t value);
void pciConfigWrite8 (uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t  value);

// ─── Bus enumeration ──────────────────────────────────────────────────────────
void enumeratePci(std::vector<PciDevice> &devices);

PciBridgeType getPciBridgeType(uint8_t classCode, uint8_t subclass);

bool isPciBridge(uint8_t classCode, uint8_t subclass);

// ─── Message loop (runs on a dedicated pthread) ───────────────────────────────
void *pciMessageLoop(void *arg);

#endif
