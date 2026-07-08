#ifndef HORIZONOS_STORAGE_MANAGER_HPP
#define HORIZONOS_STORAGE_MANAGER_HPP

#include "StorageProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace std;

constexpr uint64_t GPT_SIGNATURE = 0x5452415020494645ULL;

enum class BlockDeviceKind : uint8_t {
	WholeDisk,
	Partition,
};

struct Guid {
	uint8_t bytes[16] {};
};

struct BlockDevice {
	uint64_t id {};
	BlockDeviceKind kind {};
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	uint32_t maxPagesPerRequest {};
	uint8_t transport {};
	uint64_t readMsgBase {};
	uint64_t writeMsgBase {};
	uint64_t flushMsgBase {};
	uint64_t readReplyMsgBase {};
	uint64_t writeReplyMsgBase {};
	uint64_t flushReplyMsgBase {};
	uint64_t parentId {};
	uint64_t parentStartLba {};
	Guid partitionType {};
	Guid partitionId {};
	string name;
	string label;
};

struct FsHandler {
	uint64_t port {};
	uint16_t tid {};
	string name;
};

struct GptHeader {
	uint64_t signature {};
	uint32_t revision {};
	uint32_t headerSize {};
	uint32_t headerCrc32 {};
	uint32_t reserved {};
	uint64_t currentLba {};
	uint64_t backupLba {};
	uint64_t firstUsableLba {};
	uint64_t lastUsableLba {};
	Guid diskGuid {};
	uint64_t partitionEntryLba {};
	uint32_t partitionEntryCount {};
	uint32_t partitionEntrySize {};
	uint32_t partitionEntryArrayCrc32 {};
} __attribute__((packed));

struct GptPartitionEntry {
	Guid partitionTypeGuid {};
	Guid uniquePartitionGuid {};
	uint64_t firstLba {};
	uint64_t lastLba {};
	uint64_t attributes {};
	uint16_t name[36] {};
} __attribute__((packed));

class StorageManagerService {
public:
	auto start() -> int;
};

class StorageManagerUtils {
public:
	static auto allocateBlockDeviceIdLocked() -> uint64_t;
	static auto allocateNvmeRequestId() -> uint64_t;
	static auto validName(const char *name, size_t length, size_t maxLength, string &out) -> bool;
	static void fillName(char *dst, size_t dstSize, size_t &length, const string &name);
	static auto gptNameToString(const uint16_t *name, size_t charCount) -> string;
	static auto registerWithNameRegistry(const char *name) -> bool;
	static auto waitForService(const char *name) -> GetReplyMsgData;
	static auto findBlockDeviceLocked(uint64_t id) -> BlockDevice *;
	static auto transferBlockCount(const BlockDevice &device, uint32_t pageCount) -> uint64_t;
	static auto translateToBlockLocked(const BlockDevice &device, uint64_t lba, uint32_t pageCount, BlockDevice &out) -> bool;
	static auto currentCpuId() -> uint64_t;
	static void applyDefaultTransport(BlockDevice &device);
	static auto blockRead(const BlockDevice &device, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	static auto blockWrite(const BlockDevice &device, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	static auto blockFlush(const BlockDevice &device) -> bool;
	static auto readOnePage(const BlockDevice &device, uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool;
	static void freeOnePage(uint64_t phys, uint64_t virt);
	static void notifyFsHandlers(const BlockDevice &device);
	static void probeGpt(const BlockDevice &rawDevice);
};

#endif
