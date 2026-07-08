#ifndef HORIZONOS_VFS_HPP
#define HORIZONOS_VFS_HPP

#include <cstdint>
#include <string>

using namespace std;

constexpr uint8_t STORAGE_DEVICE_KIND_WHOLE_DISK = 0;
constexpr uint8_t STORAGE_DEVICE_KIND_PARTITION = 1;
constexpr uint64_t KERNEL_EVENT_MSG_TYPE = 0x1100;
constexpr uint64_t KERNEL_EVENT_THREAD_KILLED = 1;
constexpr uint64_t KERNEL_EVENT_PROCESS_KILLED = 2;
constexpr uint64_t VFS_CLIENT_PORT_BASE = 0xffff000000000000ULL;

struct KernelEventData {
	uint64_t eventType {};
	uint64_t pid {};
	uint64_t tid {};
};

class VfsService {
public:
	auto start() -> int;
};

enum class VfsVolumeKind : uint8_t {
	Partition,
	Devices,
	Tmp,
};

struct VfsVolume {
	VfsVolumeKind kind {};
	bool mounted { true };
	uint64_t deviceId {};
	uint64_t fsPort {};
	uint64_t mountId {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	string name;
	string sourceName;
};

struct VfsFsHandler {
	uint64_t port {};
	string name;
};

struct VfsHandle {
	uint64_t handle {};
	uint64_t ownerPort {};
	uint32_t flags {};
	uint64_t position {};
	uint64_t size {};
	uint64_t nodeId {};
	uint64_t mountId {};
	uint64_t fsPort {};
	uint8_t nodeType {};
	string path;
};

struct VfsNodeLock {
	uint64_t ownerPort {};
	uint64_t handle {};
	uint64_t mountId {};
	uint64_t nodeId {};
	uint64_t offset {};
	uint64_t length {};
	uint8_t mode {};
};

struct VfsStatCacheEntry {
	string path;
	uint8_t nodeType {};
	uint64_t size {};
	uint64_t nodeId {};
};

struct VfsPendingUnlink {
	string path;
	uint64_t mountId {};
	uint64_t nodeId {};
	uint64_t fsPort {};
};

#endif
