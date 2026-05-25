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
extern uint64_t uacpiTid;

// Messages

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t PCI_READY_MSG_TYPE = 0x10;
constexpr uint64_t PCI_READ_MSG_TYPE = 0x20;
constexpr uint64_t PCI_READ_REPLY_MSG_TYPE = 0x30;
constexpr uint64_t PCI_WRITE_MSG_TYPE = 0x40;
constexpr uint64_t PCI_MSI_ALLOC_MSG_TYPE = 0x50;
constexpr uint64_t PCI_MSI_ALLOC_REPLY_MSG_TYPE = 0x60;
constexpr uint64_t PCI_MSI_FREE_MSG_TYPE = 0x70;
constexpr uint64_t PCI_MSIX_ALLOC_MSG_TYPE = 0x80;
constexpr uint64_t PCI_MSIX_ALLOC_REPLY_MSG_TYPE = 0x90;
constexpr uint64_t PCI_MSIX_FREE_MSG_TYPE = 0xA0;
constexpr uint64_t PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE = 0xB0;
constexpr uint64_t PCI_MSIX_GLOBAL_DISABLE_MSG_TYPE = 0xC0;
constexpr uint64_t PCI_SEARCH_DEVICE_MSG_TYPE = 0xD0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE = 0xE0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_MSG_TYPE = 0xF0;

constexpr uint64_t MCFG_DONE_MSG_TYPE = 0x100;
constexpr uint64_t MCFG_SEGMENT_MSG_TYPE = 0x200;

// Name max 16 chars
struct RegisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct GetMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct CheckMsgData {
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
};

struct RegisterReplyMsgData {
	bool success {};
};

struct CheckReplyMsgData {
	bool exists {};
};

struct GetReplyMsgData {
	uint64_t port {};
	uint16_t tid {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct McfgSegmentMsgData {
	uint64_t ecamBase {};
	uint64_t segment {};
	uint64_t bbn {};
	uint8_t endBus {};
};

struct PciReadMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
};

struct PciReadReplyMsgData {
	uint32_t data {};
};

struct PciWriteMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
	uint32_t data {};
};

struct PciMsiAllocMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint64_t port {};
};

struct PciMsiAllocReplyMsgData {
	uint8_t vec {};
};

struct PciMsiFreeMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
};

struct PciMsixAllocMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t idx {};
	uint64_t port {};
};

struct PciMsixAllocReplyMsgData {
	uint8_t vec {};
};

struct PciMsixFreeMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t idx {};
	uint8_t vec {};
};

struct PciMsixGlobalEnableMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
};

struct PciMsixGlobalDisableMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
};

struct PciSearchDeviceMsgData {
	uint8_t pciClass {};
	uint8_t pciSubclass {};
	uint8_t pciProg {};
};

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
[[noreturn]] void *handleSearchDevice(void *devicesArr);
[[noreturn]] void *handlePciRead(void *arg);
[[noreturn]] void *handlePciWrite(void *arg);
[[noreturn]] void *handleMsiAlloc(void *arg);
[[noreturn]] void *handleMsiFree(void *arg);
[[noreturn]] void *handleMsixAlloc(void *arg);
[[noreturn]] void *handleMsixFree(void *arg);
[[noreturn]] void *handleMsixGlobalEnable(void *arg);
[[noreturn]] void *handleMsixGlobalDisable(void *arg);

#endif
