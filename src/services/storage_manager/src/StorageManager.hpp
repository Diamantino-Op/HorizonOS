#ifndef HORIZONOS_STORAGE_MANAGER_HPP
#define HORIZONOS_STORAGE_MANAGER_HPP

#include <cstdint>
#include <string>

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

#endif
