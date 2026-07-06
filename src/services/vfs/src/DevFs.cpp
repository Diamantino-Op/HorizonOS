#include "DevFs.hpp"

#include "horizonos/generic.h"

#include <algorithm>
#include <cstring>

using namespace std;

DevFs::DevFs(vector<VfsVolume> &volumes) : volumes(volumes) {
}

void DevFs::setPort(const uint64_t port) {
	vfsPort = port;
}

void DevFs::fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
	const size_t copyLen = min(dstSize - 1, name.size());

	memcpy(dst, name.data(), copyLen);

	dst[copyLen] = '\0';
	length = copyLen + 1;
}

auto DevFs::nodeName(const string &fsPath) -> string {
	return fsPath.starts_with("/") ? fsPath.substr(1) : fsPath;
}

auto DevFs::findNode(const string &name, VfsDeviceNode &out) -> bool {
	pthread_mutex_lock(&nodesLock);

	const auto it = ranges::find_if(nodes, [&](const VfsDeviceNode &node) -> bool {
		return node.name == name;
	});
	const bool found = it != nodes.end();

	if (found) {
		out = *it;
	}

	pthread_mutex_unlock(&nodesLock);

	return found;
}

auto DevFs::volumesText() const -> string {
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

template<typename Request, typename Reply>
auto DevFs::sendDeviceRequest(const uint64_t requestType, const uint64_t replyType, const VfsDeviceNode &node, Request &request, Reply &reply) -> bool {
	auto msg = hos_msg();

	msg.type = requestType;
	msg.port = node.devicePort;
	msg.buffer = &request;
	msg.length = sizeof(request);

	if (send_horizonos_message(vfsPort, node.devicePort, &msg) != 0) {
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

void DevFs::cleanupOwner(const uint64_t ownerPort) {
	pthread_mutex_lock(&nodesLock);

	erase_if(nodes, [&](const VfsDeviceNode &node) -> bool {
		return node.ownerPort == ownerPort;
	});

	pthread_mutex_unlock(&nodesLock);
}

auto DevFs::registerNode(const string &name, const uint64_t ownerPort, const uint64_t devicePort, const uint32_t permissions, VfsDevRegisterReplyMsgData &reply) -> bool {
	if (name.empty() or devicePort == 0) {
		reply.status = VFS_STATUS_INVALID;
		return false;
	}

	pthread_mutex_lock(&nodesLock);

	const bool exists = ranges::any_of(nodes, [&](const VfsDeviceNode &node) -> bool {
		return node.name == name;
	});

	if (!exists) {
		nodes.push_back(VfsDeviceNode {
			.name = name,
			.ownerPort = ownerPort,
			.devicePort = devicePort,
			.permissions = permissions,
		});
		reply.success = true;
		reply.status = VFS_STATUS_OK;
	} else {
		reply.status = VFS_STATUS_EXISTS;
	}

	pthread_mutex_unlock(&nodesLock);

	return reply.success;
}

auto DevFs::unregisterNode(const string &name, const uint64_t ownerPort, VfsDevUnregisterReplyMsgData &reply) -> bool {
	if (name.empty()) {
		reply.status = VFS_STATUS_INVALID;
		return false;
	}

	pthread_mutex_lock(&nodesLock);

	const auto oldSize = nodes.size();

	erase_if(nodes, [&](const VfsDeviceNode &node) -> bool {
		return node.name == name and node.ownerPort == ownerPort;
	});

	reply.success = nodes.size() != oldSize;
	reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_NOT_FOUND;

	pthread_mutex_unlock(&nodesLock);

	return reply.success;
}

auto DevFs::stat(const string &fsPath, VfsStatReplyMsgData &reply) -> bool {
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
		reply.size = volumesText().size();
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

	const string name = nodeName(fsPath);

	pthread_mutex_lock(&nodesLock);

	const auto it = ranges::find_if(nodes, [&](const VfsDeviceNode &node) -> bool {
		return node.name == name;
	});

	if (it != nodes.end()) {
		reply.success = true;
		reply.nodeType = VFS_NODE_DEVICE;
		reply.size = 0;
		reply.status = VFS_STATUS_OK;
		reply.nodeId = 0x10000 + static_cast<uint64_t>(it - nodes.begin());
	}

	pthread_mutex_unlock(&nodesLock);

	return reply.success;
}

auto DevFs::readDir(const string &fsPath, VfsReadDirReplyMsgData &reply) -> bool {
	if (fsPath != "/" and !fsPath.empty()) {
		return false;
	}

	reply.success = true;
	reply.entryCount = 2;
	reply.status = VFS_STATUS_OK;
	fillName(reply.entries[0].name, sizeof(reply.entries[0].name), reply.entries[0].nameLength, "volumes");
	reply.entries[0].nodeType = VFS_NODE_FILE;
	reply.entries[0].size = volumesText().size();
	fillName(reply.entries[1].name, sizeof(reply.entries[1].name), reply.entries[1].nameLength, "null");
	reply.entries[1].nodeType = VFS_NODE_FILE;

	pthread_mutex_lock(&nodesLock);

	for (const auto &node : nodes) {
		if (reply.entryCount >= VFS_MAX_DIR_ENTRIES) {
			reply.hasMore = true;
			break;
		}

		auto &entry = reply.entries[reply.entryCount++];

		fillName(entry.name, sizeof(entry.name), entry.nameLength, node.name);
		entry.nodeType = VFS_NODE_DEVICE;
	}

	pthread_mutex_unlock(&nodesLock);

	return true;
}

auto DevFs::read(const string &fsPath, const uint64_t offset, const uint32_t length, VfsReadReplyMsgData &reply) -> bool {
	string text;

	if (fsPath == "/volumes" or fsPath == "volumes") {
		text = volumesText();
	} else if (fsPath == "/null" or fsPath == "null") {
		text.clear();
	} else {
		VfsDeviceNode node {};
		const string name = nodeName(fsPath);

		if (!findNode(name, node)) {
			return false;
		}

		auto request = VfsDeviceReadMsgData();
		auto deviceReply = VfsDeviceReadReplyMsgData();

		fillName(request.name, sizeof(request.name), request.nameLength, name);
		request.offset = offset;
		request.length = length;

		if (!sendDeviceRequest(VFS_DEV_READ_MSG_TYPE, VFS_DEV_READ_REPLY_MSG_TYPE, node, request, deviceReply) or !deviceReply.success) {
			reply.success = false;
			return false;
		}

		reply.success = true;
		reply.bytesRead = min<uint32_t>(deviceReply.bytesRead, VFS_MAX_READ_SIZE);
		memcpy(reply.data, deviceReply.data, reply.bytesRead);

		return true;
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

auto DevFs::write(const string &fsPath, const uint64_t offset, const uint32_t length, const uint8_t *data, VfsWriteReplyMsgData &reply) -> bool {
	if (fsPath == "/null" or fsPath == "null") {
		reply.success = true;
		reply.bytesWritten = length;
		reply.size = 0;
		reply.status = VFS_STATUS_OK;
		return true;
	}

	VfsDeviceNode node {};
	const string name = nodeName(fsPath);

	if (!findNode(name, node)) {
		reply.status = VFS_STATUS_NOT_FOUND;
		return false;
	}

	auto request = VfsDeviceWriteMsgData();
	auto deviceReply = VfsDeviceWriteReplyMsgData();

	fillName(request.name, sizeof(request.name), request.nameLength, name);
	request.offset = offset;
	request.length = length;
	memcpy(request.data, data, length);

	if (!sendDeviceRequest(VFS_DEV_WRITE_MSG_TYPE, VFS_DEV_WRITE_REPLY_MSG_TYPE, node, request, deviceReply) or !deviceReply.success) {
		reply.status = deviceReply.status == 0 ? VFS_STATUS_INVALID : deviceReply.status;
		return false;
	}

	reply.success = true;
	reply.bytesWritten = deviceReply.bytesWritten;
	reply.size = 0;
	reply.status = VFS_STATUS_OK;

	return true;
}

auto DevFs::ioctl(const string &fsPath, const VfsIoctlMsgData &data, VfsIoctlReplyMsgData &reply) -> bool {
	VfsDeviceNode node {};
	const string name = nodeName(fsPath);

	if (!findNode(name, node)) {
		reply.status = VFS_STATUS_NOT_FOUND;
		return false;
	}

	auto request = VfsDeviceIoctlMsgData();
	auto deviceReply = VfsDeviceIoctlReplyMsgData();

	fillName(request.name, sizeof(request.name), request.nameLength, name);
	request.request = data.request;
	request.inputLength = min<uint32_t>(data.inputLength, VFS_MAX_READ_SIZE);
	memcpy(request.input, data.input, request.inputLength);

	if (!sendDeviceRequest(VFS_DEV_IOCTL_MSG_TYPE, VFS_DEV_IOCTL_REPLY_MSG_TYPE, node, request, deviceReply) or !deviceReply.success) {
		reply.status = deviceReply.status == 0 ? VFS_STATUS_UNSUPPORTED : deviceReply.status;
		return false;
	}

	reply.success = true;
	reply.status = VFS_STATUS_OK;
	reply.outputLength = min<uint32_t>(deviceReply.outputLength, VFS_MAX_READ_SIZE);
	memcpy(reply.output, deviceReply.output, reply.outputLength);

	return true;
}
