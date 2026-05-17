#ifndef HORIZONOS_NVME_HPP
#define HORIZONOS_NVME_HPP

#include <cstdint>

enum class FuseType {
	Normal,
	FirstCommand,
	SecondCommand
};

enum class PSDTType {
	PRPs,
	SGLsSCBA,
	SGLsSQA
};

enum class SGLType {
	DataBlock,
	BitBucket,
	Segment,
	LastSegment,
	KeyedDataBlock,
	VendorSpecific = 0xF
};

// Address only with: 0h, 2h, 3h, 4h. Offset only with: 0h, 2h, 3h. From Ah to Fh, it's defined by the binding session.
enum class SGLSubtype {
	Address,
	Offset
};

enum class StatusCodeType {
	GenericCommand,
	CommandSpecific,
	MediaDataIntegrityErr,
	VendorSpecific = 0x7
};

struct CommandDword {
	uint8_t opCode {}; // OpCode of the command to be executed.
	uint8_t fuse : 2 {}; // Specifies whether this command is part of a fused operation and if so, which command it is in the sequence.
	uint8_t reserved : 4 {};
	uint8_t psdt : 2 {}; // specifies whether PRPs or SGLs are used for any data transfer associated with the command.
	uint16_t cid {}; // specifies a unique identifier for the command when combined with the Submission Queue identifier.
};

struct Command {
	CommandDword cdw0 {};
	uint32_t nsid {}; // Specifies the namespace that this command applies to.
	uint64_t reserved {};
	uint64_t mptr {}; // Reserved for NVMe over fabrics.
	uint64_t dptrLow {}; // Specifies the data used in the command.
	uint64_t dptrHigh {}; // Specifies the data used in the command.
	CommandDword cdw10 {};
	CommandDword cdw11 {};
	CommandDword cdw12 {};
	CommandDword cdw13 {};
	CommandDword cdw14 {};
	CommandDword cdw15 {};
};

struct VendorSpecificCommand {
	CommandDword cdw0 {};
	uint32_t nsid {}; // Specifies the namespace that this command applies to.
	uint64_t reserved {};
	uint64_t mptr {}; // Reserved for NVMe over fabrics.
	uint64_t dptrLow {}; // Specifies the data used in the command.
	uint64_t dptrHigh {}; // Specifies the data used in the command.
	uint16_t ndt {}; // Indicates the number of Dwords in the data transfer.
	uint16_t ndm {}; // Indicates the number of Dwords in the metadata transfer.
	CommandDword cdw12 {};
	CommandDword cdw13 {};
	CommandDword cdw14 {};
	CommandDword cdw15 {};
};

struct PRPEntry {
	uint64_t pbao {}; // Indicates the 64-bit physical memory page address.
};

struct SGLDescriptor {
	uint64_t typeSpecificLow {};
	uint64_t typeSpecificHigh : 56 {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
};

struct SGLDataBlock {
	uint64_t address {};
	uint32_t length {};
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
};

struct SGLBitBucket {
	uint64_t reserved1 {};
	uint32_t length {};
	uint16_t reserved2 {};
	uint8_t reserved3 {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
};

struct SGLSegment {
	uint64_t address {};
	uint32_t length {};
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
};

struct SGLLastSegment {
	uint64_t address {};
	uint32_t length {};
	uint16_t reserved1 {};
	uint8_t reserved2 {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
};

struct SGLKeyedDataBlock {
	uint64_t address {};
	uint32_t length : 24 {};
	uint32_t key {};
	uint8_t subType : 4 {};
	uint8_t type : 4 {};
} __attribute__((packed, aligned(8)));

struct CompletionEntryStatus {
	uint8_t phase : 1 {}; // Identifies whether a Completion Queue entry is new.
	uint8_t statusCode : 8 {}; // Indicates a status code identifying any error or status information for the command indicated.
	uint8_t statusCodeType : 3 {}; // Indicates the status code type of the completion queue entry.
	uint8_t reserved : 2 {};
	uint8_t more : 1 {}; // If set to ‘1’, there is more status information for this command as part of the Error Information log that may be retrieved with the Get Log Page command. If cleared to ‘0’, there is no additional status information for this command.
	uint8_t dnr : 1 {}; // If set to ‘1’, indicates that if the same command is re-submitted to any controller in the NVM subsystem, then that re-submitted command is expected to fail. If cleared to ‘0’, indicates that the same command may succeed if retried.
};

struct CompletionEntry {
	CommandDword command {}; // Reserved if command does not use DWord 0.
	uint32_t reserved {};
	uint16_t sqhd {}; // Indicates the current Submission Queue Head pointer for the Submission Queue indicated in the SQ Identifier field.
	uint16_t sqid {}; // Indicates the Submission Queue to which the associated command was issued.
	uint16_t cid {}; // Indicates the identifier of the command that is being completed.
	CompletionEntryStatus status {};
};

#endif
