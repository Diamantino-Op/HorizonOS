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
	uint64_t deviceId {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	string name;
	string sourceName;
};

#endif
