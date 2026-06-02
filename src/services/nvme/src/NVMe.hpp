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

struct CoreStruct {
	uint64_t cpuId {};
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
	std::uint32_t raw {};

	[[nodiscard]] constexpr uint8_t opCode() const noexcept {
		return static_cast<uint8_t>(raw & 0xFFu);
	}

	constexpr void setOpCode(uint8_t value) noexcept {
		raw = (raw & ~0xFFu) | static_cast<std::uint32_t>(value);
	}

	[[nodiscard]] constexpr FuseType fuse() const noexcept {
		return static_cast<FuseType>((raw >> 8) & 0x3u);
	}

	constexpr void setFuse(FuseType value) noexcept {
		raw = (raw & ~(0x3u << 8)) | (static_cast<std::uint32_t>(value) << 8);
	}

	[[nodiscard]] constexpr uint8_t reserved() const noexcept {
		return static_cast<uint8_t>((raw >> 10) & 0xFu);
	}

	constexpr void setReserved(uint8_t value) noexcept {
		raw = (raw & ~(0xFu << 10)) | ((static_cast<uint32_t>(value) & 0xFu) << 10);
	}

	[[nodiscard]] constexpr PSDTType psdt() const noexcept {
		return static_cast<PSDTType>((raw >> 14) & 0x3u);
	}

	constexpr void setPsdt(PSDTType value) noexcept {
		raw = (raw & ~(0x3u << 14)) | (static_cast<uint32_t>(value) << 14);
	}

	[[nodiscard]] constexpr uint16_t cid() const noexcept {
		return static_cast<uint16_t>((raw >> 16) & 0xFFFFu);
	}

	constexpr void setCid(uint16_t value) noexcept {
		raw = (raw & 0xFFFFu) | (static_cast<uint32_t>(value) << 16);
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

class NvmeDriver {
public:
	NvmeDriver() = default;

	static void *coreHandler(void *ctx);

	// Stores the MMIO base so later calls can read and write controller registers.
	void attachRegisters(uint64_t physData, uint64_t virtData, uint64_t* base, uint64_t size, PciDevice *ownDevice) noexcept;

	// Resets the controller and waits for it to become ready.
	[[nodiscard]] bool resetController() noexcept;

	[[nodiscard]] bool enableController() noexcept;

	// Configures the admin submission and completion queues.
	[[nodiscard]] bool initializeAdminQueues() noexcept;

	// Sends a single admin command and waits for the completion entry.
	[[nodiscard]] bool submitAdminCommand(const Command& command, CompletionEntry& result) noexcept;

	// Reads the controller identification data.
	[[nodiscard]] bool identifyController() noexcept;

	// Reads namespace identification data for a specific namespace.
	[[nodiscard]] bool identifyNamespace(uint32_t namespaceId) noexcept;

	// Issues a read request for a namespace.
	[[nodiscard]] bool read(uint32_t namespaceId, uint64_t lba, void* buffer, size_t blockCount) noexcept;

	// Issues a write request for a namespace.
	[[nodiscard]] bool write(uint32_t namespaceId, uint64_t lba, const void* buffer, size_t blockCount) noexcept;

	// Flushes outstanding writes for a namespace.
	[[nodiscard]] bool flush(uint32_t namespaceId) noexcept;

	[[nodiscard]] uint32_t getNamespaceCount() const noexcept;

	[[nodiscard]] bool getActiveNamespaces(vector<uint32_t>& nsids) noexcept;

	// Shuts the controller down and clears local state.
	void shutdown() noexcept;

private:
	uint64_t dataPhys {};
	uint64_t dataVirt {};

	uint64_t* mmioBase {};
	uint64_t mmioSize {};

	PciDevice *device {};

	Command* adminSQ {};   // Admin Submission Queue (64-byte entries)
	CompletionEntry* adminCQ {};   // Admin Completion Queue (16-byte entries)
	uint32_t adminQDepth { 64 };

	uint32_t adminSQTail { 0 };
	uint32_t adminCQHead { 0 };
	uint8_t  adminCQPhase { 1 };

	uint32_t doorbellStride { 4 };

	IdentifyControllerData controllerInfo {};
	vector<NamespaceInfo> namespaces;
};

uint32_t pciRead32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
void pciWrite32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t data);

inline uint8_t *mmioBytes(uint64_t *base) noexcept {
	return reinterpret_cast<uint8_t *>(base);
}

inline uint32_t mmioRead32(uint64_t *base, size_t offset) noexcept {
	return *reinterpret_cast<uint32_t *>(mmioBytes(base) + offset);
}

inline void mmioWrite32(uint64_t *base, size_t offset, uint32_t value) noexcept {
	*reinterpret_cast<volatile uint32_t *>(mmioBytes(base) + offset) = value;
}

inline uint64_t mmioRead64(uint64_t *base, size_t offset) noexcept {
	return *reinterpret_cast<volatile uint64_t *>(mmioBytes(base) + offset);
}

inline void mmioWrite64(uint64_t *base, size_t offset, uint64_t value) noexcept {
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
