#ifndef HORIZONOS_NVME_HPP
#define HORIZONOS_NVME_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace std;

class NvmeDriver;

constexpr uint64_t PCI_READ_MSG_TYPE = 0x20;
constexpr uint64_t PCI_READ_REPLY_MSG_TYPE = 0x30;
constexpr uint64_t PCI_WRITE_MSG_TYPE = 0x40;
constexpr uint64_t PCI_WRITE_REPLY_MSG_TYPE = 0x41;
constexpr uint64_t PCI_MSIX_ALLOC_MSG_TYPE = 0x80;
constexpr uint64_t PCI_MSIX_ALLOC_REPLY_MSG_TYPE = 0x90;
constexpr uint64_t PCI_MSIX_FREE_MSG_TYPE = 0xA0;
constexpr uint64_t PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE = 0xB0;
constexpr uint64_t PCI_MSIX_GLOBAL_ENABLE_REPLY_MSG_TYPE = 0xB1;
constexpr uint64_t PCI_MSIX_GLOBAL_DISABLE_MSG_TYPE = 0xC0;
constexpr uint64_t PCI_MSIX_GLOBAL_DISABLE_REPLY_MSG_TYPE = 0xC1;

constexpr uint64_t IRQ_RECEIVE_MSG_TYPE      = 0x1000;

constexpr uint64_t NVME_READ_MSG_BASE        = 0x10000;
constexpr uint64_t NVME_WRITE_MSG_BASE       = 0x20000;
constexpr uint64_t NVME_FLUSH_MSG_BASE       = 0x30000;
constexpr uint64_t NVME_REPLY_READ_MSG_BASE  = 0x40000;
constexpr uint64_t NVME_REPLY_WRITE_MSG_BASE = 0x50000;
constexpr uint64_t NVME_REPLY_FLUSH_MSG_BASE = 0x60000;

constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_MSG_TYPE       = 0x70000;
constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE = 0x70001;

constexpr uint32_t NVME_MAX_PAGES_PER_MSG = 256;

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
	bool     isPcie;
};

// Read/write carry a fixed-size array of physical page addresses.
// pageCount <= NVME_MAX_PAGES_PER_MSG. Pages are NOT assumed contiguous.
struct NvmeReadMsgData {
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};         // number of entries in pagePhysArray
	uint64_t pagePhysArray[NVME_MAX_PAGES_PER_MSG] {};
};

struct NvmeReadReplyMsgData {
	bool     success {};
	uint32_t pageCount {};
};

struct NvmeWriteMsgData {
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[NVME_MAX_PAGES_PER_MSG] {};
};

struct NvmeWriteReplyMsgData {
	bool success {};
};

struct NvmeFlushMsgData {
	uint32_t controllerId {};
	uint32_t nsid {};
};

struct NvmeFlushReplyMsgData {
	bool success {};
};

struct StorageRegisterBlockDeviceMsgData {
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	uint32_t maxPagesPerRequest {};
	char name[32] {};
	size_t nameLength {};
};

struct StorageRegisterBlockDeviceReplyMsgData {
	bool success {};
	uint64_t deviceId {};
};

struct PciMsixAllocMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t idx {};
	uint64_t port {};
	uint64_t lapicId {};
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

struct IrqReceiveData {
	uint64_t irqNum {};
	uint64_t cpuId {};
	bool isIrq {};
};

struct CoreStruct {
	uint64_t cpuId {};
	uint64_t apicId {};
	uint64_t coreSlot {};
	uint64_t nvmePort {};
	vector<PciDevice> *nvmeDevices {};
	vector<NvmeDriver> *controllerDrivers {};
};

enum class FuseType : uint8_t {
	Normal = 0,
	FirstCommand = 1,
	SecondCommand = 2
};

enum class PSDTType : uint8_t {
	PRPs = 0,
	SGLsSCBA = 1,
	SGLsSQA = 2
};

enum class SGLType : uint8_t {
	DataBlock = 0,
	BitBucket = 1,
	Segment = 2,
	LastSegment = 3,
	KeyedDataBlock = 4,
	VendorSpecific = 0xF
};

// Address descriptors use values 0h, 2h, 3h and 4h.
// Offset descriptors use values 0h, 2h and 3h.
// Values Ah through Fh are binding-session specific.
enum class SGLSubtype : uint8_t {
	Address = 0,
	Offset = 1
};

enum class StatusCodeType : uint8_t {
	GenericCommand = 0,
	CommandSpecific = 1,
	MediaDataIntegrityErr = 2,
	VendorSpecific = 0x7
};

struct CommandDword {
	uint32_t raw {};

	constexpr auto opCode() const noexcept -> uint8_t {
		return static_cast<uint8_t>(raw & 0xFFU);
	}

	constexpr void setOpCode(const uint8_t value) noexcept {
		raw = (raw & ~0xFFU) | static_cast<uint32_t>(value);
	}

	constexpr auto fuse() const noexcept -> FuseType {
		return static_cast<FuseType>((raw >> 8) & 0x3U);
	}

	constexpr void setFuse(FuseType value) noexcept {
		raw = (raw & ~(0x3U << 8)) | (static_cast<uint32_t>(value) << 8);
	}

	constexpr auto reserved() const noexcept -> uint8_t {
		return static_cast<uint8_t>((raw >> 10) & 0xFU);
	}

	constexpr void setReserved(const uint8_t value) noexcept {
		raw = (raw & ~(0xFU << 10)) | ((static_cast<uint32_t>(value) & 0xFU) << 10);
	}

	constexpr auto psdt() const noexcept -> PSDTType {
		return static_cast<PSDTType>((raw >> 14) & 0x3U);
	}

	constexpr void setPsdt(PSDTType value) noexcept {
		raw = (raw & ~(0x3U << 14)) | (static_cast<uint32_t>(value) << 14);
	}

	constexpr auto cid() const noexcept -> uint16_t {
		return static_cast<uint16_t>((raw >> 16) & 0xFFFFU);
	}

	constexpr void setCid(const uint16_t value) noexcept {
		raw = (raw & 0xFFFFU) | (static_cast<uint32_t>(value) << 16);
	}
};

struct Command {
	CommandDword cdw0 {}; // Encodes opcode, fuse, PSDT and CID.
	uint32_t nsid {}; // Identifies the namespace that the command targets.
	uint64_t reserved {}; // Reserved by the NVMe specification.
	uint64_t mptr {}; // Metadata pointer when metadata is transferred separately.
	uint64_t dptrLow {}; // Data pointer or first PRP/SGL pointer.
	uint64_t dptrHigh {}; // Second PRP/SGL pointer or command-specific pointer.
	CommandDword cdw10 {}; // Command-specific dword 10.
	CommandDword cdw11 {}; // Command-specific dword 11.
	CommandDword cdw12 {}; // Command-specific dword 12.
	CommandDword cdw13 {}; // Command-specific dword 13.
	CommandDword cdw14 {}; // Command-specific dword 14.
	CommandDword cdw15 {}; // Command-specific dword 15.
};

struct VendorSpecificCommand {
	CommandDword cdw0 {}; // Vendor-specific opcode and command identifier.
	uint32_t nsid {}; // Identifies the namespace that the command targets.
	uint64_t reserved {}; // Reserved by the NVMe specification.
	uint64_t mptr {}; // Metadata pointer when metadata is transferred separately.
	uint64_t dptrLow {}; // Data pointer or first PRP/SGL pointer.
	uint64_t dptrHigh {}; // Second PRP/SGL pointer or command-specific pointer.
	uint16_t ndt {}; // Number of dwords in the data transfer.
	uint16_t ndm {}; // Number of dwords in the metadata transfer.
	uint32_t reserved1 {}; // Keeps the structure aligned to the 64-byte command capsule.
	CommandDword cdw12 {}; // Vendor-specific dword 12.
	CommandDword cdw13 {}; // Vendor-specific dword 13.
	CommandDword cdw14 {}; // Vendor-specific dword 14.
	CommandDword cdw15 {}; // Vendor-specific dword 15.
};

struct PRPEntry {
	uint64_t physicalPageAddress {}; // Physical address of the memory page used by the command.
};

struct SGLDescriptor {
	uint64_t typeSpecificLow {}; // First 64 bits of descriptor-specific data.
	uint64_t typeSpecificHigh : 56 {}; // High portion of the descriptor-specific payload.
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLDataBlock {
	uint64_t address {}; // Buffer address for the transfer.
	uint32_t length {}; // Length in bytes.
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLBitBucket {
	uint64_t reserved1 {}; // Reserved/unused address field.
	uint32_t length {}; // Number of bytes to discard.
	uint16_t reserved2 {};
	uint8_t reserved3 {};
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLSegment {
	uint64_t address {}; // Address of the next SGL segment.
	uint32_t length {}; // Length in bytes.
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLLastSegment {
	uint64_t address {}; // Address of the final SGL segment.
	uint32_t length {}; // Length in bytes.
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLKeyedDataBlock {
	uint64_t address {}; // Buffer address for the transfer.
	uint32_t length : 24 {}; // Length in bytes.
	uint32_t key {}; // Key value used by the controller.
	uint8_t subType : 4 {}; // SGL subtype.
	uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct CompletionEntryStatus {
	uint16_t phase : 1 {}; // Indicates whether the completion entry is new.
	uint16_t statusCode : 8 {}; // Completion status code.
	uint16_t statusCodeType : 3 {}; // Status code type.
	uint16_t reserved : 2 {};
	uint16_t more : 1 {}; // More error information is available.
	uint16_t dnr : 1 {}; // The command should not be retried.

	[[nodiscard]] constexpr uint16_t raw() const noexcept {
		return static_cast<uint16_t>((phase & 0x1u) | ((statusCode & 0xFFu) << 1) | ((statusCodeType & 0x7u) << 9) | ((reserved & 0x3u) << 12) | ((more & 0x1u) << 14) | ((dnr & 0x1u) << 15));
	}
} __attribute__((packed));

struct CompletionEntry {
	uint32_t commandSpecific {}; // Completion result or command-specific dword 0 value.
	uint32_t reserved {}; // Reserved by the NVMe specification.
	uint16_t sqhd {}; // Submission queue head pointer.
	uint16_t sqid {}; // Submission queue identifier.
	uint16_t cid {}; // Command identifier.
	CompletionEntryStatus status {}; // Completion status information.
};

struct IdentifyControllerData {
	uint16_t vid;
	uint16_t ssvid;
	char     sn[20];
	char     mn[40];
	char     fr[8];
	uint8_t  rab;
	uint8_t  ieee[3];
	uint8_t  cmic;
	uint8_t  mdts;
	uint16_t cntlid;
	uint8_t  reserved[436]; // pad from offset 24 → 344
	uint32_t nn;
} __attribute__((packed, aligned(4096)));

struct LBAFormat {
	uint16_t ms;    // Metadata Size
	uint8_t  lbads; // LBA Data Size as power of 2 (e.g. 9 = 512 bytes, 12 = 4096 bytes)
	uint8_t  rp;    // Relative Performance
} __attribute__((packed));

struct IdentifyNamespaceData {
	uint64_t  nsze;        // Namespace Size (total LBAs)
	uint64_t  ncap;        // Namespace Capacity (usable LBAs)
	uint64_t  nuse;        // Namespace Utilization
	uint8_t   nsfeat;      // Namespace Features
	uint8_t   nlbaf;       // Number of LBA Formats (0-based, so +1 for count)
	uint8_t   flbas;       // Formatted LBA Size (bits[3:0] = index into lbaf[])
	uint8_t   mc;          // Metadata Capabilities
	uint8_t   dpc;         // End-to-end Data Protection Capabilities
	uint8_t   dps;         // End-to-end Data Protection Settings
	uint8_t   reserved[98];
	LBAFormat lbaf[16];    // LBA Format Support array
} __attribute__((packed, aligned(4096)));

struct NamespaceInfo {
	uint32_t nsid;
	uint64_t totalLbas;    // nsze
	uint32_t lbaSize;      // bytes per LBA (e.g. 512 or 4096)
	bool     valid;
};

struct PrpListPage {
	uint64_t phys {};
	uint64_t virt {};
};

struct IoQueuePair {
	Command*         sq {};
	CompletionEntry* cq {};
	uint64_t         sqPhys {};
	uint64_t         cqPhys {};
	uint32_t         depth { 64 };
	uint32_t         sqTail { 0 };
	uint32_t         cqHead { 0 };
	uint8_t          cqPhase { 1 };
	uint16_t         queueId { 0 };   // NVMe queue ID (1-based, admin = 0)
	uint64_t	     completionPort { 0 }; // MSI-X vector's notify port for this queue
	uint8_t		     msixVector { 0 }; // MSI-X vector index assigned to this queue
	bool             valid { false };
};

class NvmeDriver {
public:
	NvmeDriver() = default;

	static auto coreHandler(void *ctx) -> void *;

	void attachRegisters(uint64_t physData, uint64_t virtData, uint64_t *base, uint64_t size, PciDevice *ownDevice) noexcept;
	auto resetController() const noexcept -> bool;
	auto enableController() const noexcept -> bool;
	auto initializeAdminQueues() noexcept -> bool;
	auto submitAdminCommand(const Command &command, CompletionEntry &result) noexcept -> bool;
	auto identifyController() noexcept -> bool;
	auto identifyNamespace(uint32_t namespaceId) noexcept -> bool;
	auto read(uint32_t namespaceId, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount, uint64_t coreSlot) noexcept -> bool;
	auto write(uint32_t namespaceId, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount, uint64_t coreSlot) noexcept -> bool;
	auto flush(uint32_t namespaceId, uint64_t coreSlot) noexcept -> bool;
	auto createIoQueueForCore(uint64_t coreSlot, uint16_t queueId, uint64_t lapicId) noexcept -> bool;
	auto submitIoCommand(IoQueuePair& queue, const Command &command, CompletionEntry &result) const noexcept -> bool;
	auto getNamespaceCount() const noexcept -> uint32_t;
	auto getNamespaces() const noexcept -> const vector<NamespaceInfo>&;
	auto getActiveNamespaces(vector<uint32_t> &nsIDs) noexcept -> bool;
	void shutdown() noexcept;

	void msixGlobalEnable() const noexcept;
	void msixGlobalDisable() const noexcept;
	auto msixAllocVector(uint16_t tableIndex, uint64_t notifyPort, uint64_t lapicId = 1000000) const noexcept -> uint8_t;
	void msixFreeVector(uint16_t tableIndex, uint8_t vec) const noexcept;

private:
	static auto buildChainedPrpList(const uint64_t* pagePhysArray, uint32_t pageCount, uint64_t& prp1, uint64_t& prp2, vector<PrpListPage>& prpListPages) noexcept -> bool;
	static void freePrpListPages(vector<PrpListPage>& pages) noexcept;

	uint64_t dataPhys {};
	uint64_t dataVirt {};

	uint64_t* mmioBase {};
	uint64_t mmioSize {};

	PciDevice *device {};

	Command* adminSQ {};
	CompletionEntry* adminCQ {};
	uint32_t adminQDepth { 64 };

	uint32_t adminSQTail { 0 };
	uint32_t adminCQHead { 0 };
	uint8_t  adminCQPhase { 1 };

	uint32_t doorbellStride { 4 };

	IdentifyControllerData controllerInfo {};
	vector<NamespaceInfo> namespaces;
	
	vector<IoQueuePair> ioQueues;
	uint32_t maxTransferBlocks { 0 };

	uint64_t adminCompletionPort { 0 };
	uint8_t adminMsixVector { 0 };
};

auto pciRead32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) -> uint32_t;
void pciWrite32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t data);

inline auto mmioBytes(uint64_t *base) noexcept -> uint8_t * {
	return reinterpret_cast<uint8_t *>(base);
}

inline auto mmioRead32(uint64_t *base, size_t offset) noexcept -> uint32_t {
	return *reinterpret_cast<uint32_t *>(mmioBytes(base) + offset);
}

inline void mmioWrite32(uint64_t *base, const size_t offset, const uint32_t value) noexcept {
	*reinterpret_cast<volatile uint32_t *>(mmioBytes(base) + offset) = value;
}

inline auto mmioRead64(uint64_t *base, const size_t offset) noexcept -> uint64_t {
	return *reinterpret_cast<volatile uint64_t *>(mmioBytes(base) + offset);
}

inline void mmioWrite64(uint64_t *base, const size_t offset, const uint64_t value) noexcept {
	*reinterpret_cast<uint64_t *>(mmioBytes(base) + offset) = value;
}

static_assert(sizeof(CommandDword) == 4, "CommandDword must be 4 bytes");
static_assert(sizeof(Command) == 64, "Command must be 64 bytes");
static_assert(sizeof(VendorSpecificCommand) == 64, "VendorSpecificCommand must be 64 bytes");
static_assert(sizeof(PRPEntry) == 8, "PRPEntry must be 8 bytes");
static_assert(sizeof(SGLDescriptor) == 16, "SGLDescriptor must be 16 bytes");
static_assert(sizeof(SGLDataBlock) == 16, "SGLDataBlock must be 16 bytes");
static_assert(sizeof(SGLBitBucket) == 16, "SGLBitBucket must be 16 bytes");
static_assert(sizeof(SGLSegment) == 16, "SGLSegment must be 16 bytes");
static_assert(sizeof(SGLLastSegment) == 16, "SGLLastSegment must be 16 bytes");
static_assert(sizeof(SGLKeyedDataBlock) == 16, "SGLKeyedDataBlock must be 16 bytes");
static_assert(sizeof(CompletionEntryStatus) == 2, "CompletionEntryStatus must be 2 bytes");
static_assert(sizeof(CompletionEntry) == 16, "CompletionEntry must be 16 bytes");
static_assert(sizeof(IdentifyControllerData) == 4096, "IdentifyControllerData must be 4096 bytes");
static_assert(sizeof(IdentifyNamespaceData) == 4096, "IdentifyNamespaceData must be 4096 bytes");

#endif
