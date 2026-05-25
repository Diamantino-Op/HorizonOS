#ifndef HORIZONOS_NVME_HPP
#define HORIZONOS_NVME_HPP

#include <cstddef>
#include <cstdint>

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

enum class FuseType : std::uint8_t {
	Normal = 0,
	FirstCommand = 1,
	SecondCommand = 2
};

enum class PSDTType : std::uint8_t {
	PRPs = 0,
	SGLsSCBA = 1,
	SGLsSQA = 2
};

enum class SGLType : std::uint8_t {
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
enum class SGLSubtype : std::uint8_t {
	Address = 0,
	Offset = 1
};

enum class StatusCodeType : std::uint8_t {
	GenericCommand = 0,
	CommandSpecific = 1,
	MediaDataIntegrityErr = 2,
	VendorSpecific = 0x7
};

struct CommandDword {
	std::uint32_t raw {};

	[[nodiscard]] constexpr std::uint8_t opCode() const noexcept {
		return static_cast<std::uint8_t>(raw & 0xFFu);
	}

	constexpr void setOpCode(std::uint8_t value) noexcept {
		raw = (raw & ~0xFFu) | static_cast<std::uint32_t>(value);
	}

	[[nodiscard]] constexpr FuseType fuse() const noexcept {
		return static_cast<FuseType>((raw >> 8) & 0x3u);
	}

	constexpr void setFuse(FuseType value) noexcept {
		raw = (raw & ~(0x3u << 8)) | (static_cast<std::uint32_t>(value) << 8);
	}

	[[nodiscard]] constexpr std::uint8_t reserved() const noexcept {
		return static_cast<std::uint8_t>((raw >> 10) & 0xFu);
	}

	constexpr void setReserved(std::uint8_t value) noexcept {
		raw = (raw & ~(0xFu << 10)) | ((static_cast<std::uint32_t>(value) & 0xFu) << 10);
	}

	[[nodiscard]] constexpr PSDTType psdt() const noexcept {
		return static_cast<PSDTType>((raw >> 14) & 0x3u);
	}

	constexpr void setPsdt(PSDTType value) noexcept {
		raw = (raw & ~(0x3u << 14)) | (static_cast<std::uint32_t>(value) << 14);
	}

	[[nodiscard]] constexpr std::uint16_t cid() const noexcept {
		return static_cast<std::uint16_t>((raw >> 16) & 0xFFFFu);
	}

	constexpr void setCid(std::uint16_t value) noexcept {
		raw = (raw & 0xFFFFu) | (static_cast<std::uint32_t>(value) << 16);
	}
};

struct Command {
	CommandDword cdw0 {}; // Encodes opcode, fuse, PSDT and CID.
	std::uint32_t nsid {}; // Identifies the namespace that the command targets.
	std::uint64_t reserved {}; // Reserved by the NVMe specification.
	std::uint64_t mptr {}; // Metadata pointer when metadata is transferred separately.
	std::uint64_t dptrLow {}; // Data pointer or first PRP/SGL pointer.
	std::uint64_t dptrHigh {}; // Second PRP/SGL pointer or command-specific pointer.
	CommandDword cdw10 {}; // Command-specific dword 10.
	CommandDword cdw11 {}; // Command-specific dword 11.
	CommandDword cdw12 {}; // Command-specific dword 12.
	CommandDword cdw13 {}; // Command-specific dword 13.
	CommandDword cdw14 {}; // Command-specific dword 14.
	CommandDword cdw15 {}; // Command-specific dword 15.
};

struct VendorSpecificCommand {
	CommandDword cdw0 {}; // Vendor-specific opcode and command identifier.
	std::uint32_t nsid {}; // Identifies the namespace that the command targets.
	std::uint64_t reserved {}; // Reserved by the NVMe specification.
	std::uint64_t mptr {}; // Metadata pointer when metadata is transferred separately.
	std::uint64_t dptrLow {}; // Data pointer or first PRP/SGL pointer.
	std::uint64_t dptrHigh {}; // Second PRP/SGL pointer or command-specific pointer.
	std::uint16_t ndt {}; // Number of dwords in the data transfer.
	std::uint16_t ndm {}; // Number of dwords in the metadata transfer.
	std::uint32_t reserved1 {}; // Keeps the structure aligned to the 64-byte command capsule.
	CommandDword cdw12 {}; // Vendor-specific dword 12.
	CommandDword cdw13 {}; // Vendor-specific dword 13.
	CommandDword cdw14 {}; // Vendor-specific dword 14.
	CommandDword cdw15 {}; // Vendor-specific dword 15.
};

struct PRPEntry {
	std::uint64_t physicalPageAddress {}; // Physical address of the memory page used by the command.
};

struct SGLDescriptor {
	std::uint64_t typeSpecificLow {}; // First 64 bits of descriptor-specific data.
	std::uint64_t typeSpecificHigh : 56 {}; // High portion of the descriptor-specific payload.
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLDataBlock {
	std::uint64_t address {}; // Buffer address for the transfer.
	std::uint32_t length {}; // Length in bytes.
	std::uint16_t reserved1 {};
	std::uint8_t reserved2 {};
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLBitBucket {
	std::uint64_t reserved1 {}; // Reserved/unused address field.
	std::uint32_t length {}; // Number of bytes to discard.
	std::uint16_t reserved2 {};
	std::uint8_t reserved3 {};
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLSegment {
	std::uint64_t address {}; // Address of the next SGL segment.
	std::uint32_t length {}; // Length in bytes.
	std::uint16_t reserved1 {};
	std::uint8_t reserved2 {};
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLLastSegment {
	std::uint64_t address {}; // Address of the final SGL segment.
	std::uint32_t length {}; // Length in bytes.
	std::uint16_t reserved1 {};
	std::uint8_t reserved2 {};
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct SGLKeyedDataBlock {
	std::uint64_t address {}; // Buffer address for the transfer.
	std::uint32_t length : 24 {}; // Length in bytes.
	std::uint32_t key {}; // Key value used by the controller.
	std::uint8_t subType : 4 {}; // SGL subtype.
	std::uint8_t type : 4 {}; // SGL type.
} __attribute__((packed, aligned(8)));

struct CompletionEntryStatus {
	std::uint16_t phase : 1 {}; // Indicates whether the completion entry is new.
	std::uint16_t statusCode : 8 {}; // Completion status code.
	std::uint16_t statusCodeType : 3 {}; // Status code type.
	std::uint16_t reserved : 2 {};
	std::uint16_t more : 1 {}; // More error information is available.
	std::uint16_t dnr : 1 {}; // The command should not be retried.

	[[nodiscard]] constexpr std::uint16_t raw() const noexcept {
		return static_cast<std::uint16_t>((phase & 0x1u) | ((statusCode & 0xFFu) << 1) | ((statusCodeType & 0x7u) << 9) | ((reserved & 0x3u) << 12) | ((more & 0x1u) << 14) | ((dnr & 0x1u) << 15));
	}
} __attribute__((packed));

struct CompletionEntry {
	std::uint32_t commandSpecific {}; // Completion result or command-specific dword 0 value.
	std::uint32_t reserved {}; // Reserved by the NVMe specification.
	std::uint16_t sqhd {}; // Submission queue head pointer.
	std::uint16_t sqid {}; // Submission queue identifier.
	std::uint16_t cid {}; // Command identifier.
	CompletionEntryStatus status {}; // Completion status information.
};

class NvmeDriver {
public:
	NvmeDriver() = default;

	// Stores the MMIO base so later calls can read and write controller registers.
	void attachRegisters(volatile std::uint8_t* base) noexcept;

	// Resets the controller and waits for it to become ready.
	[[nodiscard]] bool resetController() noexcept;

	// Configures the admin submission and completion queues.
	[[nodiscard]] bool initializeAdminQueues() noexcept;

	// Sends a single admin command and waits for the completion entry.
	[[nodiscard]] bool submitAdminCommand(const Command& command, CompletionEntry& completion) noexcept;

	// Reads the controller identification data.
	[[nodiscard]] bool identifyController() noexcept;

	// Reads namespace identification data for a specific namespace.
	[[nodiscard]] bool identifyNamespace(std::uint32_t namespaceId) noexcept;

	// Issues a read request for a namespace.
	[[nodiscard]] bool read(std::uint32_t namespaceId, std::uint64_t lba, void* buffer, std::size_t blockCount) noexcept;

	// Issues a write request for a namespace.
	[[nodiscard]] bool write(std::uint32_t namespaceId, std::uint64_t lba, const void* buffer, std::size_t blockCount) noexcept;

	// Flushes outstanding writes for a namespace.
	[[nodiscard]] bool flush(std::uint32_t namespaceId) noexcept;

	// Shuts the controller down and clears local state.
	void shutdown() noexcept;

private:
	volatile std::uint8_t* mmioBase {};
};

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

#endif
