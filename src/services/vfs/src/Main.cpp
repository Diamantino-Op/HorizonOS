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
	pthread_mutex_t handlesLock = PTHREAD_MUTEX_INITIALIZER;

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

			if (const VfsVolume *existing = existingMountedVolume(device.deviceId); existing != nullptr) {
				volume.fsPort = existing->fsPort;
				volume.mountId = existing->mountId;
				next.push_back(volume);
			} else if (mountExt2(device, sourceName, volume.mountId)) {
				volume.fsPort = ext2Port;
				next.push_back(volume);
			}

			++partitionIndex;
		}

		if (next.size() == volumes.size() and equal(next.begin(), next.end(), volumes.begin(), [](const VfsVolume &a, const VfsVolume &b) -> bool {
			return a.kind == b.kind and a.deviceId == b.deviceId and a.name == b.name;
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

		out.assign(path, length - 1);

		return true;
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

			return true;
		}

		if (fsPath == "/volumes" or fsPath == "volumes") {
			reply.success = true;
			reply.nodeType = VFS_NODE_FILE;
			reply.size = devfsVolumesText().size();

			return true;
		}

		if (fsPath == "/null" or fsPath == "null") {
			reply.success = true;
			reply.nodeType = VFS_NODE_FILE;
			reply.size = 0;

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
		fillName(reply.entries[0].name, sizeof(reply.entries[0].name), reply.entries[0].nameLength, "volumes");
		reply.entries[0].nodeType = VFS_NODE_FILE;
		reply.entries[0].size = devfsVolumesText().size();
		fillName(reply.entries[1].name, sizeof(reply.entries[1].name), reply.entries[1].nameLength, "null");
		reply.entries[1].nodeType = VFS_NODE_FILE;

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
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsStat(fsPath, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsStatMsgData();
		auto fsReply = FsStatReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_STAT_MSG_TYPE, FS_STAT_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;
		reply.nodeType = fsReply.nodeType;
		reply.size = fsReply.size;

		return true;
	}

	auto vfsReadDirPath(const string &path, VfsReadDirReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsReadDir(fsPath, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsReadDirMsgData();
		auto fsReply = FsReadDirReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_READDIR_MSG_TYPE, FS_READDIR_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;
		reply.entryCount = fsReply.entryCount;

		for (uint32_t i = 0; i < reply.entryCount; ++i) {
			reply.entries[i] = fsReply.entries[i];
		}

		return true;
	}

	auto vfsReadPath(const string &path, const uint64_t offset, const uint32_t length, VfsReadReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (length > VFS_MAX_READ_SIZE or !splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr) {
			return false;
		}

		if (volume->kind == VfsVolumeKind::Devices) {
			return devfsRead(fsPath, offset, length, reply);
		}

		if (volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
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

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
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
			return false;
		}

		reply.success = true;
		reply.bytesWritten = fsReply.bytesWritten;
		reply.size = fsReply.size;

		return true;
	}

	auto vfsCreatePath(const string &path, const uint8_t nodeType, VfsCreateReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsCreateMsgData();
		auto fsReply = FsCreateReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.nodeType = nodeType;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_CREATE_MSG_TYPE, FS_CREATE_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;

		return true;
	}

	auto vfsUnlinkPath(const string &path, VfsUnlinkReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsUnlinkMsgData();
		auto fsReply = FsUnlinkReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_UNLINK_MSG_TYPE, FS_UNLINK_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;

		return true;
	}

	auto vfsRenamePath(const string &oldPath, const string &newPath, VfsRenameReplyMsgData &reply) -> bool {
		string oldVolumeName;
		string oldFsPath;
		string newVolumeName;
		string newFsPath;

		if (!splitVfsPath(oldPath, oldVolumeName, oldFsPath) or !splitVfsPath(newPath, newVolumeName, newFsPath) or oldVolumeName != newVolumeName) {
			return false;
		}

		const VfsVolume *volume = findVolume(oldVolumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsRenameMsgData();
		auto fsReply = FsRenameReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fillName(fsRequest.oldPath, sizeof(fsRequest.oldPath), fsRequest.oldPathLength, oldFsPath);
		fillName(fsRequest.newPath, sizeof(fsRequest.newPath), fsRequest.newPathLength, newFsPath);

		if (!sendFsRequest(FS_RENAME_MSG_TYPE, FS_RENAME_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;

		return true;
	}

	auto vfsTruncatePath(const string &path, const uint64_t size, VfsTruncateReplyMsgData &reply) -> bool {
		string volumeName;
		string fsPath;

		if (!splitVfsPath(path, volumeName, fsPath)) {
			return false;
		}

		const VfsVolume *volume = findVolume(volumeName);

		if (volume == nullptr or volume->kind != VfsVolumeKind::Partition or volume->fsPort == 0) {
			return false;
		}

		auto fsRequest = FsTruncateMsgData();
		auto fsReply = FsTruncateReplyMsgData();

		fsRequest.mountId = volume->mountId;
		fsRequest.size = size;
		fillName(fsRequest.path, sizeof(fsRequest.path), fsRequest.pathLength, fsPath);

		if (!sendFsRequest(FS_TRUNCATE_MSG_TYPE, FS_TRUNCATE_REPLY_MSG_TYPE, *volume, fsRequest, fsReply) or !fsReply.success) {
			return false;
		}

		reply.success = true;
		reply.size = fsReply.size;

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
				vfsReadDirPath(path, reply);
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

				if (!vfsStatPath(path, stat) and (data.flags & VFS_OPEN_CREATE) != 0) {
					auto create = VfsCreateReplyMsgData();

					if (vfsCreatePath(path, VFS_NODE_FILE, create)) {
						stat = VfsStatReplyMsgData();
						vfsStatPath(path, stat);
					}
				}

				if (stat.success) {
					pthread_mutex_lock(&handlesLock);

					const uint64_t handleId = nextHandleId++;

					handles.push_back(VfsHandle {
						.handle = handleId,
						.ownerPort = msg.src_port,
						.flags = data.flags,
						.position = 0,
						.size = stat.size,
						.nodeType = stat.nodeType,
						.path = path,
					});

					pthread_mutex_unlock(&handlesLock);

					reply.success = true;
					reply.handle = handleId;
					reply.nodeType = stat.nodeType;
					reply.size = stat.size;
				}
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

			pthread_mutex_lock(&handlesLock);

			const auto it = ranges::find_if(handles, [&](const VfsHandle &handle) -> bool {
				return handle.handle == data.handle and handle.ownerPort == msg.src_port;
			});

			if (it != handles.end()) {
				handles.erase(it);
				reply.success = true;
			}

			pthread_mutex_unlock(&handlesLock);

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
					memcpy(reply.data, read.data, reply.bytesRead);
				}
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
				offset = it->position;
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
				}
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
				}
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
	pthread_t unlinkThread;
	pthread_t renameThread;
	pthread_t truncateThread;
	pthread_t handleSeekThread;

	if (pthread_create(&statThread, nullptr, statHandler, nullptr) != 0 or
	    pthread_create(&readDirThread, nullptr, readDirHandler, nullptr) != 0 or
	    pthread_create(&readThread, nullptr, readHandler, nullptr) != 0 or
	    pthread_create(&writeThread, nullptr, writeHandler, nullptr) != 0 or
	    pthread_create(&createThread, nullptr, createHandler, nullptr) != 0 or
	    pthread_create(&openThread, nullptr, openHandler, nullptr) != 0 or
	    pthread_create(&closeThread, nullptr, closeHandler, nullptr) != 0 or
	    pthread_create(&handleReadThread, nullptr, handleReadHandler, nullptr) != 0 or
	    pthread_create(&handleWriteThread, nullptr, handleWriteHandler, nullptr) != 0 or
	    pthread_create(&unlinkThread, nullptr, unlinkHandler, nullptr) != 0 or
	    pthread_create(&renameThread, nullptr, renameHandler, nullptr) != 0 or
	    pthread_create(&truncateThread, nullptr, truncateHandler, nullptr) != 0 or
	    pthread_create(&handleSeekThread, nullptr, handleSeekHandler, nullptr) != 0) {
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
	pthread_detach(unlinkThread);
	pthread_detach(renameThread);
	pthread_detach(truncateThread);
	pthread_detach(handleSeekThread);

	for (;;) {
		refreshVolumes();
		usleep(500000);
	}
}
