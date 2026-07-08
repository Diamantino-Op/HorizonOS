#ifndef HORIZONOS_VFS_HPP
#define HORIZONOS_VFS_HPP

#include "VfsProtocol.hpp"

#include <cstdint>
#include <string>
#include <vector>

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

class VfsUtils {
public:
	static void fillName(char *dst, size_t dstSize, size_t &length, const string &name);
	static auto validName(const char *name, size_t length, size_t maxLength, string &out) -> bool;
	static auto clientPortForOwner(uint64_t pid, uint64_t tid) -> uint64_t;
	static auto ownerPidFromClientPort(uint64_t ownerPort) -> uint64_t;
	static void invalidatePathCache(const string &path = "");
	static auto isPendingUnlinkPath(const string &path) -> bool;
	static auto registerWithNameRegistry(const char *name) -> bool;
	static auto waitForService(const char *name) -> GetReplyMsgData;
	static auto isReservedVolumeName(const string &name) -> bool;
	static auto sanitizeVolumeStem(const string &candidate, uint32_t fallbackIndex) -> string;
	static auto uniqueVolumeName(const string &stem, const vector<VfsVolume> &existing) -> string;
	static auto partitionVolumeStem(const string &partitionLabel, const string &deviceName, uint32_t fallbackIndex) -> string;
	static void addReservedVolumes(vector<VfsVolume> &out);
	static auto existingMountedVolumeLocked(uint64_t deviceId) -> const VfsVolume *;
	static auto existingVolumeByDeviceLocked(uint64_t deviceId) -> const VfsVolume *;
	static auto volumeCount() -> size_t;
	static auto listStorageDevices(StorageListBlockDevicesReplyMsgData &reply) -> bool;
	static auto mountFs(uint64_t fsPort, const StorageListedBlockDevice &device, const string &sourceName, uint64_t &mountId) -> bool;
	static auto mountAnyFilesystem(const StorageListedBlockDevice &device, const string &sourceName, uint64_t &fsPort, uint64_t &mountId) -> bool;
	static auto listedDeviceHasPartition(const StorageListBlockDevicesReplyMsgData &reply, uint64_t parentId) -> bool;
	static auto shouldTryMountListedDevice(const StorageListBlockDevicesReplyMsgData &reply, const StorageListedBlockDevice &device) -> bool;
	static void refreshVolumes();
	static auto splitVfsPath(const string &path, string &volumeName, string &fsPath) -> bool;
	static auto normalizeFsPath(const string &input, string &out) -> bool;
	static auto normalizeVfsPath(const string &input, string &out) -> bool;
	static auto findVolume(const string &name, VfsVolume &out) -> bool;
	static auto validPath(const char *path, size_t length, string &out) -> bool;

	template<typename Request, typename Reply>
	static auto sendFsRequest(uint64_t requestType, uint64_t replyType, const VfsVolume &volume, Request &request, Reply &reply) -> bool;

	static auto vfsStatPath(const string &path, VfsStatReplyMsgData &reply) -> bool;
	static auto vfsReadDirPath(const string &path, uint32_t offset, VfsReadDirReplyMsgData &reply) -> bool;
	static auto vfsReadPath(const string &path, uint64_t offset, uint32_t length, VfsReadReplyMsgData &reply) -> bool;
	static auto vfsWritePath(const string &path, uint64_t offset, uint32_t length, const uint8_t *data, VfsWriteReplyMsgData &reply) -> bool;
	static auto vfsCreatePath(const string &path, uint8_t nodeType, VfsCreateReplyMsgData &reply) -> bool;
	static auto vfsMkdirPath(const string &path, VfsMkdirReplyMsgData &reply) -> bool;
	static auto vfsUnlinkPath(const string &path, VfsUnlinkReplyMsgData &reply) -> bool;
	static auto vfsCommitPendingUnlink(const VfsPendingUnlink &pending) -> bool;
	static void cleanupOwnerPort(uint64_t ownerPort);
	static void cleanupOwnerProcess(uint64_t pid);
	static auto vfsCopyPath(const string &oldPath, const string &newPath, VfsCopyReplyMsgData &reply) -> bool;
	static auto vfsRenamePath(const string &oldPath, const string &newPath, VfsRenameReplyMsgData &reply) -> bool;
	static auto vfsTruncatePath(const string &path, uint64_t size, VfsTruncateReplyMsgData &reply) -> bool;
	static auto vfsLinkPath(const string &oldPath, const string &newPath, VfsLinkReplyMsgData &reply) -> bool;
	static auto vfsSymlinkPath(const string &target, const string &linkPath, VfsSymlinkReplyMsgData &reply) -> bool;
	static auto vfsReadLinkPath(const string &path, VfsReadLinkReplyMsgData &reply) -> bool;
	static auto lockRangesOverlap(uint64_t aOffset, uint64_t aLength, uint64_t bOffset, uint64_t bLength) -> bool;
	static auto findHandleForOwner(uint64_t handleId, uint64_t ownerPort, VfsHandle &out) -> bool;
	static auto vfsSyncVolume(const string &volumeName, uint32_t &status) -> bool;
};

#endif
