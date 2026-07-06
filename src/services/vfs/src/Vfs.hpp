#ifndef HORIZONOS_VFS_HPP
#define HORIZONOS_VFS_HPP

#include <cstdint>
#include <string>

using namespace std;

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
