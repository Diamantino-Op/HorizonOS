#include "Vfs.hpp"
#include "VfsProtocol.hpp"

#include "horizonos/generic.h"
#include "unistd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

namespace {
	constexpr uint8_t STORAGE_DEVICE_KIND_PARTITION = 1;

	uint64_t vfsPort = 0;
	uint64_t storagePort = 0;
	uint64_t ext2Port = 0;
	uint64_t nextHandleId = 1;
	vector<VfsVolume> volumes;
	vector<VfsHandle> handles;
	vector<VfsNodeLock> nodeLocks;
	vector<VfsStatCacheEntry> statCache;
	vector<VfsDeviceNode> deviceNodes;
	vector<VfsPendingUnlink> pendingUnlinks;
	pthread_mutex_t handlesLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t nodeLocksLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t cacheLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t deviceNodesLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t pendingUnlinksLock = PTHREAD_MUTEX_INITIALIZER;

	void fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());

		memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto validName(const char *name, const size_t length, const size_t maxLength, string &out) -> bool {
		if (length == 0 or length > maxLength or name[length - 1] != '\0') {
			return false;
		}

		out.assign(name, length - 1);

		return true;
	}

	void invalidatePathCache(const string &path = "") {
		pthread_mutex_lock(&cacheLock);

		if (path.empty()) {
			statCache.clear();
		} else {
			statCache.erase(remove_if(statCache.begin(), statCache.end(), [&](const VfsStatCacheEntry &entry) -> bool {
				return entry.path == path or entry.path.starts_with(path + "/");
			}), statCache.end());
		}

		pthread_mutex_unlock(&cacheLock);
	}

	auto isPendingUnlinkPath(const string &path) -> bool {
		pthread_mutex_lock(&pendingUnlinksLock);

		const bool pending = ranges::any_of(pendingUnlinks, [&](const VfsPendingUnlink &unlink) -> bool {
			return unlink.path == path;
		});

		pthread_mutex_unlock(&pendingUnlinksLock);

		return pending;
	}

	auto registerWithNameRegistry(const char *name) -> bool {
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		fillName(data.name, sizeof(data.name), data.nameLength, name);

		auto msg = hos_msg();

		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(vfsPort, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(vfsPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto waitForService(const char *name) -> GetReplyMsgData {
		for (;;) {
			auto check = CheckMsgData();

			fillName(check.name, sizeof(check.name), check.nameLength, name);

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(vfsPort, 1, &checkMsg);

			auto checkReply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &checkReply;
			recv.length = sizeof(checkReply);

			auto filter = filter_options();

			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(vfsPort, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and checkReply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();

		fillName(get.name, sizeof(get.name), get.nameLength, name);

		auto getMsg = hos_msg();

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &get;
		getMsg.length = sizeof(get);

		send_horizonos_message(vfsPort, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(vfsPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply;
	}

	auto isReservedVolumeName(const string &name) -> bool {
		return name == "Devices" or name == "Tmp";
	}

	auto sanitizeVolumeStem(const string &candidate, const uint32_t fallbackIndex) -> string {
		string out;

		for (const char ch : candidate) {
			if ((ch >= 'A' and ch <= 'Z') or (ch >= 'a' and ch <= 'z') or (ch >= '0' and ch <= '9') or ch == '_' or ch == '-') {
				out.push_back(ch);
			} else if (ch == ' ' and !out.empty()) {
				out.push_back('_');
			}
		}

		while (!out.empty() and out.back() == '_') {
			out.pop_back();
		}

		if (out.empty()) {
			out = "Volume" + to_string(fallbackIndex);
		}

		if (isReservedVolumeName(out)) {
			out += "Volume";
		}

		return out;
	}

	auto uniqueVolumeName(const string &stem, const vector<VfsVolume> &existing) -> string {
		string candidate = stem + ":";
		uint32_t suffix = 2;

		while (ranges::any_of(existing, [&](const VfsVolume &volume) -> bool {
			return volume.name == candidate;
		})) {
			candidate = stem + to_string(suffix++) + ":";
		}

		return candidate;
	}

	auto partitionVolumeStem(const string &partitionLabel, const string &deviceName, const uint32_t fallbackIndex) -> string {
		const string labelStem = sanitizeVolumeStem(partitionLabel, fallbackIndex);

		if (!partitionLabel.empty() and !labelStem.empty()) {
			return labelStem;
		}

		return sanitizeVolumeStem(deviceName, fallbackIndex);
	}

	void addReservedVolumes(vector<VfsVolume> &out) {
		VfsVolume devices {};
		devices.kind = VfsVolumeKind::Devices;
		devices.name = "Devices:";
		devices.sourceName = "devfs";
		out.push_back(devices);

		VfsVolume tmp {};
		tmp.kind = VfsVolumeKind::Tmp;
		tmp.name = "Tmp:";
		tmp.sourceName = "tmpfs";
		out.push_back(tmp);
	}

	auto existingMountedVolume(const uint64_t deviceId) -> const VfsVolume * {
		const auto it = ranges::find_if(volumes, [&](const VfsVolume &volume) -> bool {
			return volume.kind == VfsVolumeKind::Partition and volume.deviceId == deviceId and volume.fsPort != 0 and volume.mountId != 0;
		});

		return it == volumes.end() ? nullptr : &(*it);
	}

	auto existingVolumeByDevice(const uint64_t deviceId) -> const VfsVolume * {
		const auto it = ranges::find_if(volumes, [&](const VfsVolume &volume) -> bool {
			return volume.kind == VfsVolumeKind::Partition and volume.deviceId == deviceId;
		});

		return it == volumes.end() ? nullptr : &(*it);
	}

	auto listStorageDevices(StorageListBlockDevicesReplyMsgData &reply) -> bool {
		auto data = StorageListBlockDevicesMsgData();
		auto msg = hos_msg();

		msg.type = STORAGE_LIST_BLOCK_DEVICES_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(vfsPort, storagePort, &msg) != 0) {
			return false;
		}

		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_LIST_BLOCK_DEVICES_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(vfsPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto mountExt2(const StorageListedBlockDevice &device, const string &sourceName, uint64_t &mountId) -> bool {
		auto data = FsMountMsgData();

		data.deviceId = device.deviceId;
		data.blockCount = device.blockCount;
		data.blockSize = device.blockSize;
		fillName(data.deviceName, sizeof(data.deviceName), data.deviceNameLength, sourceName);

		auto msg = hos_msg();

		msg.type = FS_MOUNT_MSG_TYPE;
		msg.port = ext2Port;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(vfsPort, ext2Port, &msg) != 0) {
			return false;
		}

		auto reply = FsMountReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_MOUNT_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(vfsPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		if (ret == 0 and reply.success) {
			mountId = reply.mountId;

			return true;
		}

		return false;
	}

	void refreshVolumes() {
		auto reply = StorageListBlockDevicesReplyMsgData();

		if (!listStorageDevices(reply)) {
			printf("Vfs: Failed to list storage block devices.");
			fflush(stdout);

			return;
		}

		vector<VfsVolume> next;

		addReservedVolumes(next);

		uint32_t partitionIndex = 1;

		for (uint32_t i = 0; i < reply.deviceCount; ++i) {
			const StorageListedBlockDevice &device = reply.devices[i];

			if (device.kind != STORAGE_DEVICE_KIND_PARTITION) {
				continue;
			}

			string label;
			string sourceName;

			validName(device.label, device.labelLength, sizeof(device.label), label);
			validName(device.name, device.nameLength, sizeof(device.name), sourceName);

			const string stem = partitionVolumeStem(label, sourceName, partitionIndex);

			VfsVolume volume {};

			volume.kind = VfsVolumeKind::Partition;
			volume.deviceId = device.deviceId;
			volume.blockCount = device.blockCount;
			volume.blockSize = device.blockSize;
			volume.sourceName = sourceName;
			volume.name = uniqueVolumeName(stem, next);
			volume.mounted = true;

			if (const VfsVolume *existingAny = existingVolumeByDevice(device.deviceId); existingAny != nullptr and !existingAny->mounted) {
				volume.mounted = false;
				volume.name = existingAny->name;
				next.push_back(volume);
			} else if (const VfsVolume *existing = existingMountedVolume(device.deviceId); existing != nullptr) {
				volume.name = existing->name;
				volume.fsPort = existing->fsPort;
				volume.mountId = existing->mountId;
				volume.mounted = existing->mounted;
				next.push_back(volume);
			} else if (mountExt2(device, sourceName, volume.mountId)) {
				volume.fsPort = ext2Port;
				next.push_back(volume);
			}

			++partitionIndex;
		}

		if (next.size() == volumes.size() and equal(next.begin(), next.end(), volumes.begin(), [](const VfsVolume &a, const VfsVolume &b) -> bool {
			return a.kind == b.kind and a.deviceId == b.deviceId and a.name == b.name and a.mounted == b.mounted;
		})) {
			return;
		}

		volumes = next;

		printf("Vfs: Volume table:");
		fflush(stdout);

		for (const auto &volume : volumes) {
			printf(" %s", volume.name.c_str());
			fflush(stdout);
		}
	}

	auto splitVfsPath(const string &path, string &volumeName, string &fsPath) -> bool {
		const size_t colon = path.find(':');

		if (colon == string::npos) {
			return false;
		}

		volumeName = path.substr(0, colon + 1);
		fsPath = path.substr(colon + 1);

		if (fsPath.empty()) {
			fsPath = "/";
		}

		return !volumeName.empty();
	}

	auto normalizeFsPath(const string &input, string &out) -> bool {
		vector<string> parts;
		size_t start = 0;

		while (start < input.size()) {
			while (start < input.size() and input[start] == '/') {
				++start;
			}

			const size_t end = input.find('/', start);
			const size_t partEnd = end == string::npos ? input.size() : end;

			if (partEnd > start) {
				const string part = input.substr(start, partEnd - start);

				if (part == ".") {
				} else if (part == "..") {
					if (parts.empty()) {
						return false;
					}

					parts.pop_back();
				} else {
					parts.push_back(part);
				}
			}

			if (end == string::npos) {
				break;
			}

			start = end + 1;
		}

		out = "/";

		for (size_t i = 0; i < parts.size(); ++i) {
			if (i != 0) {
				out += "/";
			}

			out += parts[i];
		}

		return out.size() < VFS_MAX_PATH_LENGTH;
	}

	auto normalizeVfsPath(const string &input, string &out) -> bool {
		string volumeName;
		string fsPath;
		string normalizedFsPath;

		if (!splitVfsPath(input, volumeName, fsPath) or !normalizeFsPath(fsPath, normalizedFsPath)) {
			return false;
		}

		out = volumeName + normalizedFsPath;

		return out.size() < VFS_MAX_PATH_LENGTH;
	}

	auto findVolume(const string &name) -> const VfsVolume * {
		const auto it = ranges::find_if(volumes, [&](const VfsVolume &volume) -> bool {
			return volume.name == name;
		});

		return it == volumes.end() ? nullptr : &(*it);
	}

	auto validPath(const char *path, const size_t length, string &out) -> bool {
		if (length == 0 or length > VFS_MAX_PATH_LENGTH or path[length - 1] != '\0') {
			return false;
		}

		string raw(path, length - 1);

		return normalizeVfsPath(raw, out);
	}

	template<typename Request, typename Reply>
	auto sendFsRequest(const uint64_t requestType, const uint64_t replyType, const VfsVolume &volume, Request &request, Reply &reply) -> bool {
		auto msg = hos_msg();

		msg.type = requestType;
		msg.port = volume.fsPort;
		msg.buffer = &request;
		msg.length = sizeof(request);

		if (send_horizonos_message(vfsPort, volume.fsPort, &msg) != 0) {
			return false;
		}

		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { replyType };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(vfsPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0;
	}

	auto devfsVolumesText() -> string {
		string text;

		for (const auto &volume : volumes) {
			text += volume.name;
			text += " ";
			text += volume.mounted ? "mounted " : "unmounted ";
			text += volume.sourceName.empty() ? "virtual" : volume.sourceName;
			text += "\n";
		}

		return text;
	}

	auto devfsStat(const string &fsPath, VfsStatReplyMsgData &reply) -> bool {
		if (fsPath == "/" or fsPath.empty()) {
			reply.success = true;
			reply.nodeType = VFS_NODE_DIRECTORY;
			reply.size = 0;
			reply.status = VFS_STATUS_OK;
			reply.nodeId = 1;

			return true;
		}

		if (fsPath == "/volumes" or fsPath == "volumes") {
			reply.success = true;
			reply.nodeType = VFS_NODE_FILE;
			reply.size = devfsVolumesText().size();
			reply.status = VFS_STATUS_OK;
			reply.nodeId = 2;

			return true;
		}

		if (fsPath == "/null" or fsPath == "null") {
			reply.success = true;
			reply.nodeType = VFS_NODE_FILE;
			reply.size = 0;
			reply.status = VFS_STATUS_OK;
			reply.nodeId = 3;

			return true;
		}

		string nodeName = fsPath.starts_with("/") ? fsPath.substr(1) : fsPath;

		pthread_mutex_lock(&deviceNodesLock);

		const auto it = ranges::find_if(deviceNodes, [&](const VfsDeviceNode &node) -> bool {
			return node.name == nodeName;
		});

		if (it != deviceNodes.end()) {
			reply.success = true;
			reply.nodeType = VFS_NODE_DEVICE;
			reply.size = 0;
			reply.status = VFS_STATUS_OK;
			reply.nodeId = 0x10000 + static_cast<uint64_t>(it - deviceNodes.begin());
		}

		pthread_mutex_unlock(&deviceNodesLock);

		if (reply.success) {
			return true;
		}

		return false;
	}

	auto devfsReadDir(const string &fsPath, VfsReadDirReplyMsgData &reply) -> bool {
		if (fsPath != "/" and !fsPath.empty()) {
			return false;
		}

		reply.success = true;
		reply.entryCount = 2;
		reply.status = VFS_STATUS_OK;
		fillName(reply.entries[0].name, sizeof(reply.entries[0].name), reply.entries[0].nameLength, "volumes");
		reply.entries[0].nodeType = VFS_NODE_FILE;
		reply.entries[0].size = devfsVolumesText().size();
		fillName(reply.entries[1].name, sizeof(reply.entries[1].name), reply.entries[1].nameLength, "null");
		reply.entries[1].nodeType = VFS_NODE_FILE;

		pthread_mutex_lock(&deviceNodesLock);

		for (const auto &node : deviceNodes) {
			if (reply.entryCount >= VFS_MAX_DIR_ENTRIES) {
				reply.hasMore = true;
				break;
			}

			auto &entry = reply.entries[reply.entryCount++];

			fillName(entry.name, sizeof(entry.name), entry.nameLength, node.name);
			entry.nodeType = VFS_NODE_DEVICE;
		}

		pthread_mutex_unlock(&deviceNodesLock);

		return true;
	}

	auto devfsRead(const string &fsPath, const uint64_t offset, const uint32_t length, VfsReadReplyMsgData &reply) -> bool {
		string text;

		if (fsPath == "/volumes" or fsPath == "volumes") {
			text = devfsVolumesText();
		} else if (fsPath == "/null" or fsPath == "null") {
			text.clear();
		} else {
			return false;
		}

		reply.success = true;

		if (offset >= text.size() or length == 0) {
			reply.bytesRead = 0;

			return true;
		}

		reply.bytesRead = min<uint32_t>(length, text.size() - offset);
		memcpy(reply.data, text.data() + offset, reply.bytesRead);

		return true;
	}

	auto vfsStatPath(const string &path, VfsStatReplyMsgData &reply) -> bool {
		if (isPendingUnlinkPath(path)) {
			reply.status = VFS_STATUS_NOT_FOUND;
			return false;
		}

		pthread_mutex_lock(&cacheLock);

		const auto cacheIt = ranges::find_if(statCache, [&](const VfsStatCacheEntry &entry) -> bool {
			return entry.path == path;
		});

		if (cacheIt != statCache.end()) {
			reply.success = true;
			reply.nodeType = cacheIt->nodeType;
			reply.size = cacheIt->size;
			reply.nodeId = cacheIt->nodeId;
			reply.status = VFS_STATUS_OK;
			pthread_mutex_unlock(&cacheLock);

			return true;
		}

		pthread_mutex_unlock(&cacheLock);

		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			reply.status = VFS_STATUS_NOT_FOUND;
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsStat(fsPath, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume->mounted ? VFS_STATUS_UNSUPPORTED : VFS_STATUS_NOT_FOUND;
			return false;
		}

		auto fsRequest = FsStatMsgData();
		auto fsReply = FsStatReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_STAT_MSG_TYPE, FS_STAT_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_NOT_FOUND : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.nodeType = fsReply.nodeType;
		reply.size = fsReply.size;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = fsReply.nodeId;

		pthread_mutex_lock(&cacheLock);

		if (statCache.size() >= 64) {
			statCache.erase(statCache.begin());
		}

		statCache.push_back(VfsStatCacheEntry {
			.path = path,
			.nodeType = reply.nodeType,
			.size = reply.size,
			.nodeId = reply.nodeId,
		});

		pthread_mutex_unlock(&cacheLock);

		return true;
	}

	auto vfsReadDirPath(const string &path, const uint32_t offset, VfsReadDirReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			reply.status = VFS_STATUS_NOT_FOUND;
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsReadDir(fsPath, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume->mounted ? VFS_STATUS_UNSUPPORTED : VFS_STATUS_NOT_FOUND;
			return false;
		}

		auto fsRequest = FsReadDirMsgData();
		auto fsReply = FsReadDirReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.offset = offset;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_READDIR_MSG_TYPE, FS_READDIR_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_NOT_FOUND : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.entryCount = fsReply.entryCount;
		reply.status = VFS_STATUS_OK;
		reply.nextOffset = fsReply.nextOffset;
		reply.hasMore = fsReply.hasMore;

		for (uint32_t i = 0; i < reply.entryCount; ++i) {
			reply.entries[i] = fsReply.entries[i];
		}

		return true;
	}

	auto vfsReadPath(const string &path, const uint64_t offset, const uint32_t length, VfsReadReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (length > VFS_MAX_READ_SIZE or !splitVfsPath(path, volumeName, fsPath)) {
			reply.success = false;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsRead(fsPath, offset, length, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsReadMsgData();
		auto fsReply = FsReadReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.offset = offset;
		fsRequest.length = length;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_READ_MSG_TYPE, FS_READ_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;
		reply.bytesRead = fsReply.bytesRead;
		memcpy(reply.data, fsReply.data, reply.bytesRead);

		return true;
	}

	auto vfsWritePath(const string &path, const uint64_t offset, const uint32_t length, const uint8_t *data, VfsWriteReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (length > VFS_MAX_READ_SIZE or !splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsWriteMsgData();
		auto fsReply = FsWriteReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.offset = offset;
		fsRequest.length = length;
		memcpy(fsRequest.data, data, length);
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_WRITE_MSG_TYPE, FS_WRITE_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.bytesWritten = fsReply.bytesWritten;
		reply.size = fsReply.size;
		reply.status = VFS_STATUS_OK;
		invalidatePathCache(path);

		return true;
	}

	auto vfsCreatePath(const string &path, const uint8_t nodeType, VfsCreateReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsCreateMsgData();
		auto fsReply = FsCreateReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.nodeType = nodeType;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_CREATE_MSG_TYPE, FS_CREATE_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = fsReply.nodeId;
		invalidatePathCache(path);

		return true;
	}

	auto vfsMkdirPath(const string &path, VfsMkdirReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsMkdirMsgData();
		auto fsReply = FsMkdirReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_MKDIR_MSG_TYPE, FS_MKDIR_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = fsReply.nodeId;
		invalidatePathCache(path);

		return true;
	}

	auto vfsUnlinkPath(const string &path, VfsUnlinkReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsUnlinkMsgData();
		auto fsReply = FsUnlinkReplyMsgData();
		auto stat = VfsStatReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!vfsStatPath(path, stat)) {
			reply.status = stat.status == 0 ? VFS_STATUS_NOT_FOUND : stat.status;
			return false;
		}

		pthread_mutex_lock(&handlesLock);

		const bool openNode = ranges::any_of(handles, [&](const VfsHandle &handle) -> bool {
			return handle.mountId == volume->mountId and handle.nodeId == stat.nodeId and handle.fsPort == volume->fsPort;
		});

		pthread_mutex_unlock(&handlesLock);

		if (openNode) {
			pthread_mutex_lock(&pendingUnlinksLock);

			const bool alreadyPending = ranges::any_of(pendingUnlinks, [&](const VfsPendingUnlink &unlink) -> bool {
				return unlink.mountId == volume->mountId and unlink.nodeId == stat.nodeId and unlink.fsPort == volume->fsPort;
			});

			if (!alreadyPending) {
				pendingUnlinks.push_back(VfsPendingUnlink {
					.path = path,
					.mountId = volume->mountId,
					.nodeId = stat.nodeId,
					.fsPort = volume->fsPort,
				});
			}

			pthread_mutex_unlock(&pendingUnlinksLock);

			reply.success = true;
			reply.status = VFS_STATUS_OK;
			invalidatePathCache(path);

			return true;
		}

		if (!sendFsRequest(FS_UNLINK_MSG_TYPE, FS_UNLINK_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		invalidatePathCache(path);

		return true;
	}

	auto vfsCommitPendingUnlink(const VfsPendingUnlink &pending) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(pending.path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsUnlinkMsgData();
		auto fsReply = FsUnlinkReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_UNLINK_MSG_TYPE, FS_UNLINK_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		invalidatePathCache(pending.path);

		return true;
	}

	auto vfsCopyPath(const string &oldPath, const string &newPath, VfsCopyReplyMsgData &reply) -> bool {
		auto stat = VfsStatReplyMsgData();
		auto create = VfsCreateReplyMsgData();

		if (!vfsStatPath(oldPath, stat)) {
			reply.status = stat.status == 0 ? VFS_STATUS_NOT_FOUND : stat.status;
			return false;
		}

		if (stat.nodeType != VFS_NODE_FILE) {
			reply.status = VFS_STATUS_UNSUPPORTED;
			return false;
		}

		if (!vfsCreatePath(newPath, VFS_NODE_FILE, create)) {
			reply.status = create.status == 0 ? VFS_STATUS_INVALID : create.status;
			return false;
		}

		uint64_t offset = 0;

		while (offset < stat.size) {
			auto read = VfsReadReplyMsgData();
			auto write = VfsWriteReplyMsgData();

			if (!vfsReadPath(oldPath, offset, VFS_MAX_READ_SIZE, read) or !read.success or read.bytesRead == 0) {
				reply.status = offset >= stat.size ? VFS_STATUS_OK : VFS_STATUS_INVALID;
				reply.success = offset >= stat.size;
				reply.bytesCopied = offset;

				return reply.success;
			}

			if (!vfsWritePath(newPath, offset, read.bytesRead, read.data, write) or !write.success) {
				reply.status = write.status == 0 ? VFS_STATUS_INVALID : write.status;
				reply.bytesCopied = offset;
				return false;
			}

			offset += read.bytesRead;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		reply.bytesCopied = offset;

		return true;
	}

	auto vfsRenamePath(const string &oldPath, const string &newPath, VfsRenameReplyMsgData &reply) -> bool {
		string oldVolumeName;
		string oldFsPath;
		string newVolumeName;
		string newFsPath;

		if (!splitVfsPath(oldPath, oldVolumeName, oldFsPath) or !splitVfsPath(newPath, newVolumeName, newFsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		if (oldVolumeName != newVolumeName) {
			auto copy = VfsCopyReplyMsgData();
			auto unlink = VfsUnlinkReplyMsgData();

			if (!vfsCopyPath(oldPath, newPath, copy)) {
				reply.status = copy.status == 0 ? VFS_STATUS_INVALID : copy.status;
				return false;
			}

			if (!vfsUnlinkPath(oldPath, unlink)) {
				reply.status = unlink.status == 0 ? VFS_STATUS_INVALID : unlink.status;
				return false;
			}

			reply.success = true;
			reply.status = VFS_STATUS_OK;

			return true;
		}

		const VfsVolume *volume = findVolume(oldVolumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsRenameMsgData();
		auto fsReply = FsRenameReplyMsgData();
		auto oldStat = VfsStatReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.oldPath, sizeof(fsRequest.oldPath), fsRequest.oldPathLength, oldFsPath);
		fillName(fsRequest.newPath, sizeof(fsRequest.newPath), fsRequest.newPathLength, newFsPath);

		if (!vfsStatPath(oldPath, oldStat)) {
			reply.status = oldStat.status == 0 ? VFS_STATUS_NOT_FOUND : oldStat.status;
			return false;
		}

		if (!sendFsRequest(FS_RENAME_MSG_TYPE, FS_RENAME_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		invalidatePathCache(oldPath);
		invalidatePathCache(newPath);

		pthread_mutex_lock(&handlesLock);

		for (auto &handle : handles) {
			if (handle.mountId == volume->mountId and handle.nodeId == oldStat.nodeId and handle.fsPort == volume->fsPort) {
				handle.path = newPath;
			}
		}

		pthread_mutex_unlock(&handlesLock);

		return true;
	}

	auto vfsTruncatePath(const string &path, const uint64_t size, VfsTruncateReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto fsRequest = FsTruncateMsgData();
		auto fsReply = FsTruncateReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.size = size;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_TRUNCATE_MSG_TYPE, FS_TRUNCATE_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.size = fsReply.size;
		reply.status = VFS_STATUS_OK;
		invalidatePathCache(path);

		return true;
	}

	auto vfsLinkPath(const string &oldPath, const string &newPath, VfsLinkReplyMsgData &reply) -> bool {
		string oldVolumeName;
		string oldFsPath;
		string newVolumeName;
		string newFsPath;

		if (!splitVfsPath(oldPath, oldVolumeName, oldFsPath) or !splitVfsPath(newPath, newVolumeName, newFsPath) or oldVolumeName != newVolumeName) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(oldVolumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto request = FsLinkMsgData();
		auto fsReply = FsLinkReplyMsgData();

		request.mountId = volume->mountId;
		fillName(request.oldPath, sizeof(request.oldPath), request.oldPathLength, oldFsPath);
		fillName(request.newPath, sizeof(request.newPath), request.newPathLength, newFsPath);

		if (!sendFsRequest(FS_LINK_MSG_TYPE, FS_LINK_REPLY_MSG_TYPE, *volume, request, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = fsReply.nodeId;
		invalidatePathCache(newPath);

		return true;
	}

	auto vfsSymlinkPath(const string &target, const string &linkPath, VfsSymlinkReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(linkPath, volumeName, fsPath)) {
			reply.status = VFS_STATUS_INVALID;
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			reply.status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto request = FsSymlinkMsgData();
		auto fsReply = FsSymlinkReplyMsgData();

		request.mountId = volume->mountId;
		fillName(request.target, sizeof(request.target), request.targetLength, target);
		fillName(request.linkPath, sizeof(request.linkPath), request.linkPathLength, fsPath);

		if (!sendFsRequest(FS_SYMLINK_MSG_TYPE, FS_SYMLINK_REPLY_MSG_TYPE, *volume, request, fsReply) or !fsReply.success) {
			reply.status = fsReply.status == 0 ? VFS_STATUS_INVALID : fsReply.status;
			return false;
		}

		reply.success = true;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = fsReply.nodeId;
		invalidatePathCache(linkPath);

		return true;
	}

	auto lockRangesOverlap(const uint64_t aOffset, const uint64_t aLength, const uint64_t bOffset, const uint64_t bLength) -> bool {
		const uint64_t aEnd = aLength == 0 ? UINT64_MAX : aOffset + aLength;
		const uint64_t bEnd = bLength == 0 ? UINT64_MAX : bOffset + bLength;

		return aOffset < bEnd and bOffset < aEnd;
	}

	auto findHandleForOwner(const uint64_t handleId, const uint64_t ownerPort, VfsHandle &out) -> bool {
		pthread_mutex_lock(&handlesLock);

		const auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
			return handle.handle == handleId and handle.ownerPort == ownerPort;
		});
		const bool found = it != handles.end();

		if (found) {
			out = *it;
		}

		pthread_mutex_unlock(&handlesLock);

		return found;
	}

	auto vfsSyncVolume(const string &volumeName, uint32_t &status) -> bool {
		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or !volume->mounted or volume->fsPort == 0) {
			status = volume == nullptr ? VFS_STATUS_NOT_FOUND : VFS_STATUS_UNSUPPORTED;
			return false;
		}

		auto request = FsSyncMsgData();
		auto reply = FsSyncReplyMsgData();

		request.mountId = volume->mountId;

		if (!sendFsRequest(FS_SYNC_MSG_TYPE, FS_SYNC_REPLY_MSG_TYPE, *volume, request, reply) or !reply.success) {
			status = reply.status == 0 ? VFS_STATUS_INVALID : reply.status;
			return false;
		}

		status = VFS_STATUS_OK;

		return true;
	}

	[[noreturn]] auto statHandler(void */*unused*/) -> void * {
		auto data = VfsStatMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_STAT_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsStatReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsStatPath(path, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_STAT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto readDirHandler(void */*unused*/) -> void * {
		auto data = VfsReadDirMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_READDIR_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsReadDirReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsReadDirPath(path, data.offset, reply);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_READDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto readHandler(void */*unused*/) -> void * {
		auto data = VfsReadMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_READ_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsReadReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsReadPath(path, data.offset, data.length, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_READ_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto writeHandler(void */*unused*/) -> void * {
		auto data = VfsWriteMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_WRITE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsWriteReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsWritePath(path, data.offset, data.length, data.data, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_WRITE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto createHandler(void */*unused*/) -> void * {
		auto data = VfsCreateMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_CREATE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsCreateReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsCreatePath(path, data.nodeType, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_CREATE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto mkdirHandler(void */*unused*/) -> void * {
		auto data = VfsMkdirMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_MKDIR_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsMkdirReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsMkdirPath(path, reply);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_MKDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto unlinkHandler(void */*unused*/) -> void * {
		auto data = VfsUnlinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_UNLINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsUnlinkReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsUnlinkPath(path, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_UNLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto renameHandler(void */*unused*/) -> void * {
		auto data = VfsRenameMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_RENAME_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsRenameReplyMsgData();
			string oldPath;
			string newPath;

			if (validPath(data.oldPath, data.oldPathLength, oldPath) and validPath(data.newPath, data.newPathLength, newPath)) {
				vfsRenamePath(oldPath, newPath, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_RENAME_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto truncateHandler(void */*unused*/) -> void * {
		auto data = VfsTruncateMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_TRUNCATE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsTruncateReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				vfsTruncatePath(path, data.size, reply);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_TRUNCATE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto openHandler(void */*unused*/) -> void * {
		auto data = VfsOpenMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_OPEN_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsOpenReplyMsgData();
			string path;

			if (validPath(data.path, data.pathLength, path)) {
				auto stat = VfsStatReplyMsgData();

				const bool existed = vfsStatPath(path, stat);

				if (existed and (data.flags & VFS_OPEN_CREATE) != 0 and (data.flags & VFS_OPEN_EXCLUSIVE) != 0) {
					reply.status = VFS_STATUS_EXISTS;
				} else if (!existed and (data.flags & VFS_OPEN_CREATE) != 0) {
					auto create = VfsCreateReplyMsgData();

					if (vfsCreatePath(path, VFS_NODE_FILE, create)) {
						stat = VfsStatReplyMsgData();
						vfsStatPath(path, stat);
					} else {
						reply.status = create.status;
					}
				} else if (!existed) {
					reply.status = stat.status == 0 ? VFS_STATUS_NOT_FOUND : stat.status;
				}

				if (stat.success) {
					if ((data.flags & VFS_OPEN_TRUNCATE) != 0 and (data.flags & VFS_OPEN_WRITE) != 0 and stat.nodeType == VFS_NODE_FILE) {
						auto truncate = VfsTruncateReplyMsgData();

						if (vfsTruncatePath(path, 0, truncate)) {
							stat.size = 0;
						} else {
							reply.status = truncate.status;
							stat.success = false;
						}
					}
				}

				if (stat.success) {
					string volumeName;
					string fsPath;
					uint64_t mountId = 0;
					uint64_t fsPort = 0;

					if (splitVfsPath(path, volumeName, fsPath)) {
						if (const VfsVolume *volume = findVolume(volumeName); volume != nullptr) {
							mountId = volume->mountId;
							fsPort = volume->fsPort;
						}
					}

					pthread_mutex_lock(&handlesLock);

					const uint64_t handleId = nextHandleId++;

					handles.push_back(VfsHandle {
						.handle = handleId,
						.ownerPort = msg.src_port,
						.flags = data.flags,
						.position = (data.flags & VFS_OPEN_APPEND) != 0 ? stat.size : 0,
						.size = stat.size,
						.nodeId = stat.nodeId,
						.mountId = mountId,
						.fsPort = fsPort,
						.nodeType = stat.nodeType,
						.path = path,
					});

					pthread_mutex_unlock(&handlesLock);

					reply.success = true;
					reply.handle = handleId;
					reply.nodeType = stat.nodeType;
					reply.size = stat.size;
					reply.status = VFS_STATUS_OK;
					reply.nodeId = stat.nodeId;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_OPEN_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto closeHandler(void */*unused*/) -> void * {
		auto data = VfsCloseMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_CLOSE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsCloseReplyMsgData();
			auto pending = VfsPendingUnlink();
			bool commitPendingUnlink = false;

			pthread_mutex_lock(&handlesLock);

			const auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end()) {
				const VfsHandle closedHandle = *it;

				pthread_mutex_lock(&nodeLocksLock);

				nodeLocks.erase(remove_if(nodeLocks.begin(), nodeLocks.end(), [&](const VfsNodeLock &lock) -> bool {
					return lock.ownerPort == msg.src_port and lock.handle == data.handle;
				}), nodeLocks.end());

				pthread_mutex_unlock(&nodeLocksLock);

				handles.erase(it);

				const bool stillOpen = ranges::any_of(handles, [&](const VfsHandle &handle) -> bool {
					return handle.mountId == closedHandle.mountId and handle.nodeId == closedHandle.nodeId and handle.fsPort == closedHandle.fsPort;
				});

				if (!stillOpen) {
					pthread_mutex_lock(&pendingUnlinksLock);

					const auto pendingIt = ranges::find_if(pendingUnlinks, [&](const VfsPendingUnlink &unlink) -> bool {
						return unlink.mountId == closedHandle.mountId and unlink.nodeId == closedHandle.nodeId and unlink.fsPort == closedHandle.fsPort;
					});

					if (pendingIt != pendingUnlinks.end()) {
						pending = *pendingIt;
						pendingUnlinks.erase(pendingIt);
						commitPendingUnlink = true;
					}

					pthread_mutex_unlock(&pendingUnlinksLock);
				}

				reply.success = true;
				reply.status = VFS_STATUS_OK;
			} else {
				reply.status = VFS_STATUS_NOT_FOUND;
			}

			pthread_mutex_unlock(&handlesLock);

			if (commitPendingUnlink) {
				vfsCommitPendingUnlink(pending);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_CLOSE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto handleReadHandler(void */*unused*/) -> void * {
		auto data = VfsHandleReadMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_HANDLE_READ_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsHandleReadReplyMsgData();
			string path;
			uint64_t offset = 0;
			bool allowed = false;

			pthread_mutex_lock(&handlesLock);

			auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end() and (it->flags & VFS_OPEN_READ) != 0 and it->nodeType == VFS_NODE_FILE) {
				path = it->path;
				offset = it->position;
				allowed = true;
			}

			pthread_mutex_unlock(&handlesLock);

			if (allowed) {
				auto read = VfsReadReplyMsgData();

				if (vfsReadPath(path, offset, data.length, read) and read.success) {
					pthread_mutex_lock(&handlesLock);

					it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
						return handle.handle == data.handle and handle.ownerPort == msg.src_port;
					});

					if (it != handles.end()) {
						it->position += read.bytesRead;
						reply.position = it->position;
					}

					pthread_mutex_unlock(&handlesLock);

					reply.success = true;
					reply.bytesRead = read.bytesRead;
					reply.status = VFS_STATUS_OK;
					memcpy(reply.data, read.data, reply.bytesRead);
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_HANDLE_READ_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto handleWriteHandler(void */*unused*/) -> void * {
		auto data = VfsHandleWriteMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_HANDLE_WRITE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsHandleWriteReplyMsgData();
			string path;
			uint64_t offset = 0;
			bool allowed = false;

			pthread_mutex_lock(&handlesLock);

			auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end() and (it->flags & VFS_OPEN_WRITE) != 0 and it->nodeType == VFS_NODE_FILE) {
				path = it->path;
				offset = (it->flags & VFS_OPEN_APPEND) != 0 ? it->size : it->position;
				allowed = true;
			}

			pthread_mutex_unlock(&handlesLock);

			if (allowed) {
				auto write = VfsWriteReplyMsgData();

				if (vfsWritePath(path, offset, data.length, data.data, write) and write.success) {
					pthread_mutex_lock(&handlesLock);

					it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
						return handle.handle == data.handle and handle.ownerPort == msg.src_port;
					});

					if (it != handles.end()) {
						it->position += write.bytesWritten;
						it->size = write.size;
						reply.position = it->position;
						reply.size = it->size;
					}

					pthread_mutex_unlock(&handlesLock);

					reply.success = true;
					reply.bytesWritten = write.bytesWritten;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_HANDLE_WRITE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto handleSeekHandler(void */*unused*/) -> void * {
		auto data = VfsHandleSeekMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_HANDLE_SEEK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsHandleSeekReplyMsgData();

			pthread_mutex_lock(&handlesLock);

			auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end()) {
				int64_t base = 0;

				if (data.whence == VFS_SEEK_SET) {
					base = 0;
				} else if (data.whence == VFS_SEEK_CUR) {
					base = static_cast<int64_t>(it->position);
				} else if (data.whence == VFS_SEEK_END) {
					base = static_cast<int64_t>(it->size);
				}

				const int64_t next = base + data.offset;

				if ((data.whence == VFS_SEEK_SET or data.whence == VFS_SEEK_CUR or data.whence == VFS_SEEK_END) and next >= 0) {
					it->position = static_cast<uint64_t>(next);
					reply.success = true;
					reply.position = it->position;
					reply.status = VFS_STATUS_OK;
				}
			}
			if (!reply.success) {
				reply.status = reply.status == 0 ? VFS_STATUS_INVALID : reply.status;
			}

			pthread_mutex_unlock(&handlesLock);

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_HANDLE_SEEK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto handleReadDirHandler(void */*unused*/) -> void * {
		auto data = VfsHandleReadDirMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_HANDLE_READDIR_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsHandleReadDirReplyMsgData();
			string path;
			uint64_t offset = 0;
			bool allowed = false;

			pthread_mutex_lock(&handlesLock);

			auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end() and (it->flags & VFS_OPEN_READ) != 0 and it->nodeType == VFS_NODE_DIRECTORY) {
				path = it->path;
				offset = it->position;
				allowed = true;
			}

			pthread_mutex_unlock(&handlesLock);

			if (allowed) {
				auto readDir = VfsReadDirReplyMsgData();

				if (vfsReadDirPath(path, static_cast<uint32_t>(offset), readDir) and readDir.success) {
					pthread_mutex_lock(&handlesLock);

					it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
						return handle.handle == data.handle and handle.ownerPort == msg.src_port;
					});

					if (it != handles.end()) {
						it->position = readDir.nextOffset;
						reply.position = it->position;
					}

					pthread_mutex_unlock(&handlesLock);

					reply.success = true;
					reply.entryCount = readDir.entryCount;
					reply.hasMore = readDir.hasMore;
					reply.status = VFS_STATUS_OK;

					for (uint32_t i = 0; i < reply.entryCount; ++i) {
						reply.entries[i] = readDir.entries[i];
					}
				} else {
					reply.status = readDir.status == 0 ? VFS_STATUS_INVALID : readDir.status;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_HANDLE_READDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto lockHandler(void */*unused*/) -> void * {
		auto data = VfsLockMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_LOCK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsLockReplyMsgData();
			VfsHandle handle {};

			if (!findHandleForOwner(data.handle, msg.src_port, handle) or (data.mode != VFS_LOCK_SHARED and data.mode != VFS_LOCK_EXCLUSIVE)) {
				reply.status = VFS_STATUS_INVALID;
			} else {
				pthread_mutex_lock(&nodeLocksLock);

				const bool conflict = ranges::any_of(nodeLocks, [&](const VfsNodeLock &lock) -> bool {
					if (lock.mountId != handle.mountId or lock.nodeId != handle.nodeId or lock.handle == handle.handle) {
						return false;
					}

					if (!lockRangesOverlap(lock.offset, lock.length, data.offset, data.length)) {
						return false;
					}

					return lock.mode == VFS_LOCK_EXCLUSIVE or data.mode == VFS_LOCK_EXCLUSIVE;
				});

				if (!conflict) {
					nodeLocks.push_back(VfsNodeLock {
						.ownerPort = msg.src_port,
						.handle = handle.handle,
						.mountId = handle.mountId,
						.nodeId = handle.nodeId,
						.offset = data.offset,
						.length = data.length,
						.mode = data.mode,
					});

					reply.success = true;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_BUSY;
				}

				pthread_mutex_unlock(&nodeLocksLock);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_LOCK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto unlockHandler(void */*unused*/) -> void * {
		auto data = VfsUnlockMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_UNLOCK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsUnlockReplyMsgData();

			pthread_mutex_lock(&nodeLocksLock);

			const auto oldSize = nodeLocks.size();

			nodeLocks.erase(remove_if(nodeLocks.begin(), nodeLocks.end(), [&](const VfsNodeLock &lock) -> bool {
				return lock.ownerPort == msg.src_port and lock.handle == data.handle and
				       lockRangesOverlap(lock.offset, lock.length, data.offset, data.length);
			}), nodeLocks.end());

			reply.success = nodeLocks.size() != oldSize;
			reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_NOT_FOUND;

			pthread_mutex_unlock(&nodeLocksLock);

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_UNLOCK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto syncHandler(void */*unused*/) -> void * {
		auto data = VfsSyncMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_SYNC_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsSyncReplyMsgData();
			string volumeName;

			if (validName(data.volume, data.volumeLength, sizeof(data.volume), volumeName)) {
				reply.success = vfsSyncVolume(volumeName, reply.status);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_SYNC_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto fsyncHandler(void */*unused*/) -> void * {
		auto data = VfsFsyncMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_FSYNC_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsFsyncReplyMsgData();
			VfsHandle handle {};

			if (findHandleForOwner(data.handle, msg.src_port, handle)) {
				string volumeName;
				string fsPath;

				if (splitVfsPath(handle.path, volumeName, fsPath)) {
					reply.success = vfsSyncVolume(volumeName, reply.status);
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_NOT_FOUND;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_FSYNC_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto copyHandler(void */*unused*/) -> void * {
		auto data = VfsCopyMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_COPY_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsCopyReplyMsgData();
			string oldPath;
			string newPath;

			if (validPath(data.oldPath, data.oldPathLength, oldPath) and validPath(data.newPath, data.newPathLength, newPath)) {
				vfsCopyPath(oldPath, newPath, reply);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_COPY_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto linkHandler(void */*unused*/) -> void * {
		auto data = VfsLinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_LINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsLinkReplyMsgData();
			string oldPath;
			string newPath;

			if (validPath(data.oldPath, data.oldPathLength, oldPath) and validPath(data.newPath, data.newPathLength, newPath)) {
				vfsLinkPath(oldPath, newPath, reply);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_LINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto symlinkHandler(void */*unused*/) -> void * {
		auto data = VfsSymlinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_SYMLINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsSymlinkReplyMsgData();
			string target;
			string linkPath;

			if (validName(data.target, data.targetLength, sizeof(data.target), target) and validPath(data.linkPath, data.linkPathLength, linkPath)) {
				vfsSymlinkPath(target, linkPath, reply);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_SYMLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto devRegisterHandler(void */*unused*/) -> void * {
		auto data = VfsDevRegisterMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_DEV_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsDevRegisterReplyMsgData();
			string name;

			if (data.devicePort != 0 and validName(data.name, data.nameLength, sizeof(data.name), name)) {
				pthread_mutex_lock(&deviceNodesLock);

				const bool exists = ranges::any_of(deviceNodes, [&](const VfsDeviceNode &node) -> bool {
					return node.name == name;
				});

				if (!exists) {
					deviceNodes.push_back(VfsDeviceNode {
						.name = name,
						.ownerPort = msg.src_port,
						.devicePort = data.devicePort,
						.permissions = data.permissions,
					});
					reply.success = true;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_EXISTS;
				}

				pthread_mutex_unlock(&deviceNodesLock);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_DEV_REGISTER_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto devUnregisterHandler(void */*unused*/) -> void * {
		auto data = VfsDevUnregisterMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_DEV_UNREGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsDevUnregisterReplyMsgData();
			string name;

			if (validName(data.name, data.nameLength, sizeof(data.name), name)) {
				pthread_mutex_lock(&deviceNodesLock);

				const auto oldSize = deviceNodes.size();

				deviceNodes.erase(remove_if(deviceNodes.begin(), deviceNodes.end(), [&](const VfsDeviceNode &node) -> bool {
					return node.name == name and node.ownerPort == msg.src_port;
				}), deviceNodes.end());

				reply.success = deviceNodes.size() != oldSize;
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_NOT_FOUND;

				pthread_mutex_unlock(&deviceNodesLock);
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_DEV_UNREGISTER_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto ioctlHandler(void */*unused*/) -> void * {
		auto data = VfsIoctlMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_IOCTL_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsIoctlReplyMsgData();
			VfsHandle handle {};

			if (findHandleForOwner(data.handle, msg.src_port, handle) and handle.nodeType == VFS_NODE_DEVICE) {
				reply.status = VFS_STATUS_UNSUPPORTED;
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_IOCTL_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto mountRefreshHandler(void */*unused*/) -> void * {
		auto data = VfsMountRefreshMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_MOUNT_REFRESH_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			refreshVolumes();

			auto reply = VfsMountRefreshReplyMsgData();

			reply.success = true;
			reply.status = VFS_STATUS_OK;
			reply.volumeCount = volumes.size();

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_MOUNT_REFRESH_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto unmountHandler(void */*unused*/) -> void * {
		auto data = VfsUnmountMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_UNMOUNT_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(vfsPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = VfsUnmountReplyMsgData();
			string volumeName;

			if (validName(data.volume, data.volumeLength, sizeof(data.volume), volumeName)) {
				auto it = ranges::find_if(volumes, [&](const VfsVolume &volume) -> bool {
					return volume.name == volumeName and volume.kind == VfsVolumeKind::Partition;
				});

				if (it != volumes.end()) {
					it->mounted = false;
					it->fsPort = 0;
					it->mountId = 0;
					reply.success = true;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();
			replyMsg.type = VFS_UNMOUNT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(vfsPort, msg.src_port, &replyMsg);
		}
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	if (register_horizonos_port(reinterpret_cast<long *>(&vfsPort)) != 0) {
		printf("Vfs: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!registerWithNameRegistry("Vfs")) {
		printf("Vfs: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	const GetReplyMsgData storageInfo = waitForService("StorageManager");
	storagePort = storageInfo.port;
	const GetReplyMsgData ext2Info = waitForService("Ext2");
	ext2Port = ext2Info.port;

	printf("Vfs: Ready on port %lu, Storage port %lu, Ext2 port %lu.", vfsPort, storagePort, ext2Port);
	fflush(stdout);

	pthread_t statThread;
	pthread_t readDirThread;
	pthread_t readThread;
	pthread_t writeThread;
	pthread_t createThread;
	pthread_t openThread;
	pthread_t closeThread;
	pthread_t handleReadThread;
	pthread_t handleWriteThread;
	pthread_t handleReadDirThread;
	pthread_t unlinkThread;
	pthread_t renameThread;
	pthread_t truncateThread;
	pthread_t handleSeekThread;
	pthread_t mkdirThread;
	pthread_t mountRefreshThread;
	pthread_t unmountThread;
	pthread_t lockThread;
	pthread_t unlockThread;
	pthread_t syncThread;
	pthread_t fsyncThread;
	pthread_t copyThread;
	pthread_t linkThread;
	pthread_t symlinkThread;
	pthread_t devRegisterThread;
	pthread_t devUnregisterThread;
	pthread_t ioctlThread;

	if (pthread_create(&statThread, nullptr, statHandler, nullptr) != 0 or
	    pthread_create(&readDirThread, nullptr, readDirHandler, nullptr) != 0 or
	    pthread_create(&readThread, nullptr, readHandler, nullptr) != 0 or
	    pthread_create(&writeThread, nullptr, writeHandler, nullptr) != 0 or
	    pthread_create(&createThread, nullptr, createHandler, nullptr) != 0 or
	    pthread_create(&openThread, nullptr, openHandler, nullptr) != 0 or
	    pthread_create(&closeThread, nullptr, closeHandler, nullptr) != 0 or
	    pthread_create(&handleReadThread, nullptr, handleReadHandler, nullptr) != 0 or
	    pthread_create(&handleWriteThread, nullptr, handleWriteHandler, nullptr) != 0 or
	    pthread_create(&handleReadDirThread, nullptr, handleReadDirHandler, nullptr) != 0 or
	    pthread_create(&unlinkThread, nullptr, unlinkHandler, nullptr) != 0 or
	    pthread_create(&renameThread, nullptr, renameHandler, nullptr) != 0 or
	    pthread_create(&truncateThread, nullptr, truncateHandler, nullptr) != 0 or
	    pthread_create(&handleSeekThread, nullptr, handleSeekHandler, nullptr) != 0 or
	    pthread_create(&mkdirThread, nullptr, mkdirHandler, nullptr) != 0 or
	    pthread_create(&mountRefreshThread, nullptr, mountRefreshHandler, nullptr) != 0 or
	    pthread_create(&unmountThread, nullptr, unmountHandler, nullptr) != 0 or
	    pthread_create(&lockThread, nullptr, lockHandler, nullptr) != 0 or
	    pthread_create(&unlockThread, nullptr, unlockHandler, nullptr) != 0 or
	    pthread_create(&syncThread, nullptr, syncHandler, nullptr) != 0 or
	    pthread_create(&fsyncThread, nullptr, fsyncHandler, nullptr) != 0 or
	    pthread_create(&copyThread, nullptr, copyHandler, nullptr) != 0 or
	    pthread_create(&linkThread, nullptr, linkHandler, nullptr) != 0 or
	    pthread_create(&symlinkThread, nullptr, symlinkHandler, nullptr) != 0 or
	    pthread_create(&devRegisterThread, nullptr, devRegisterHandler, nullptr) != 0 or
	    pthread_create(&devUnregisterThread, nullptr, devUnregisterHandler, nullptr) != 0 or
	    pthread_create(&ioctlThread, nullptr, ioctlHandler, nullptr) != 0) {
		printf("Vfs: Failed to create message handlers.");
		fflush(stdout);

		return 1;
	}

	pthread_detach(statThread);
	pthread_detach(readDirThread);
	pthread_detach(readThread);
	pthread_detach(writeThread);
	pthread_detach(createThread);
	pthread_detach(openThread);
	pthread_detach(closeThread);
	pthread_detach(handleReadThread);
	pthread_detach(handleWriteThread);
	pthread_detach(handleReadDirThread);
	pthread_detach(unlinkThread);
	pthread_detach(renameThread);
	pthread_detach(truncateThread);
	pthread_detach(handleSeekThread);
	pthread_detach(mkdirThread);
	pthread_detach(mountRefreshThread);
	pthread_detach(unmountThread);
	pthread_detach(lockThread);
	pthread_detach(unlockThread);
	pthread_detach(syncThread);
	pthread_detach(fsyncThread);
	pthread_detach(copyThread);
	pthread_detach(linkThread);
	pthread_detach(symlinkThread);
	pthread_detach(devRegisterThread);
	pthread_detach(devUnregisterThread);
	pthread_detach(ioctlThread);

	for (;;) {
		refreshVolumes();
		usleep(500000);
	}
}
