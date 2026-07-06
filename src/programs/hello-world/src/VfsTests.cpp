#include "VfsTests.hpp"

#include "VfsProtocol.hpp"
#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

namespace {
	constexpr uint32_t TEST_IOCTL_REQUEST = 0x48575646;

	uint64_t devicePort {};
	std::atomic<uint32_t> deviceRequestsHandled { 0 };
	std::atomic<bool> deviceThreadReady { false };

	constexpr size_t DEVICE_REQUEST_BUFFER_SIZE = std::max({
		sizeof(VfsDeviceReadMsgData),
		sizeof(VfsDeviceWriteMsgData),
		sizeof(VfsDeviceIoctlMsgData),
	});

	void fillName(char *dst, const size_t dstSize, size_t &length, const char *name) {
		const size_t copyLen = std::min(dstSize - 1, strlen(name));
		memcpy(dst, name, copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	void fillBytes(uint8_t *dst, const size_t dstSize, const char *text, uint32_t &length) {
		length = std::min<uint32_t>(strlen(text), dstSize);
		memcpy(dst, text, length);
	}

	auto bytesEqual(const uint8_t *data, const uint32_t length, const char *expected) -> bool {
		const size_t expectedLength = strlen(expected);
		return length == expectedLength and memcmp(data, expected, expectedLength) == 0;
	}

	template<typename Request, typename Reply>
	auto vfsRequest(const uint64_t requestType, Request &request, Reply &reply) -> bool {
		return sendVfsRequest(requestType, &request, sizeof(request), &reply, sizeof(reply)) == 0;
	}

	template<typename Reply>
	auto vfsRequestNoData(const uint64_t requestType, Reply &reply) -> bool {
		return sendVfsRequest(requestType, nullptr, 0, &reply, sizeof(reply)) == 0;
	}

	auto pass(const char *name) -> bool {
		printf("[VFS test] PASS %s", name);
		fflush(stdout);
		return true;
	}

	auto fail(const char *name, const char *reason) -> bool {
		printf("[VFS test] FAIL %s: %s", name, reason);
		fflush(stdout);
		return false;
	}

	auto expectOk(const char *name, const bool condition, const char *reason) -> bool {
		return condition ? pass(name) : fail(name, reason);
	}

	struct VolumeList {
		char names[16][VFS_MAX_NAME_LENGTH] {};
		uint32_t count {};
	};

	auto requestMountRefresh(uint32_t *volumeCount = nullptr) -> bool {
		auto request = VfsMountRefreshMsgData();
		auto reply = VfsMountRefreshReplyMsgData();

		if (!vfsRequest(VFS_MOUNT_REFRESH_MSG_TYPE, request, reply) or !reply.success or reply.status != VFS_STATUS_OK) {
			return false;
		}

		if (volumeCount != nullptr) {
			*volumeCount = reply.volumeCount;
		}

		return true;
	}

	auto createFile(const char *path) -> bool {
		auto request = VfsCreateMsgData();
		auto reply = VfsCreateReplyMsgData();

		request.nodeType = VFS_NODE_FILE;
		fillName(request.path, sizeof(request.path), request.pathLength, path);

		return vfsRequest(VFS_CREATE_MSG_TYPE, request, reply) and reply.success and reply.status == VFS_STATUS_OK;
	}

	auto mkdirPath(const char *path) -> bool {
		auto request = VfsMkdirMsgData();
		auto reply = VfsMkdirReplyMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);

		return vfsRequest(VFS_MKDIR_MSG_TYPE, request, reply) and reply.success and reply.status == VFS_STATUS_OK;
	}

	auto unlinkPath(const char *path, uint32_t *statusOut = nullptr) -> bool {
		auto request = VfsUnlinkMsgData();
		auto reply = VfsUnlinkReplyMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);

		if (!vfsRequest(VFS_UNLINK_MSG_TYPE, request, reply)) {
			return false;
		}

		if (statusOut != nullptr) {
			*statusOut = reply.status;
		}

		return reply.success and reply.status == VFS_STATUS_OK;
	}

	auto statPath(const char *path, VfsStatReplyMsgData &reply) -> bool {
		auto request = VfsStatMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);

		return vfsRequest(VFS_STAT_MSG_TYPE, request, reply);
	}

	auto pathMissing(const char *path) -> bool {
		auto reply = VfsStatReplyMsgData();

		return statPath(path, reply) and !reply.success and reply.status == VFS_STATUS_NOT_FOUND;
	}

	auto writePath(const char *path, const char *payload) -> bool {
		auto request = VfsWriteMsgData();
		auto reply = VfsWriteReplyMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);
		fillBytes(request.data, sizeof(request.data), payload, request.length);

		return vfsRequest(VFS_WRITE_MSG_TYPE, request, reply) and reply.success and reply.status == VFS_STATUS_OK and reply.bytesWritten == request.length;
	}

	auto readPath(const char *path, char *out, const size_t outSize, uint32_t &outLength) -> bool {
		auto request = VfsReadMsgData();
		auto reply = VfsReadReplyMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);
		request.length = VFS_MAX_READ_SIZE;

		if (!vfsRequest(VFS_READ_MSG_TYPE, request, reply) or !reply.success) {
			return false;
		}

		outLength = std::min<uint32_t>(reply.bytesRead, outSize == 0 ? 0 : outSize - 1);

		if (outSize != 0) {
			memcpy(out, reply.data, outLength);
			out[outLength] = '\0';
		}

		return true;
	}

	auto symlinkPath(const char *target, const char *linkPath) -> bool {
		auto request = VfsSymlinkMsgData();
		auto reply = VfsSymlinkReplyMsgData();

		fillName(request.target, sizeof(request.target), request.targetLength, target);
		fillName(request.linkPath, sizeof(request.linkPath), request.linkPathLength, linkPath);

		return vfsRequest(VFS_SYMLINK_MSG_TYPE, request, reply) and reply.success and reply.status == VFS_STATUS_OK;
	}

	auto readLinkPath(const char *path, char *out, const size_t outSize) -> bool {
		auto request = VfsReadLinkMsgData();
		auto reply = VfsReadLinkReplyMsgData();

		fillName(request.path, sizeof(request.path), request.pathLength, path);

		if (!vfsRequest(VFS_READLINK_MSG_TYPE, request, reply) or !reply.success or reply.status != VFS_STATUS_OK or reply.targetLength == 0) {
			return false;
		}

		const size_t copyLen = std::min(outSize - 1, reply.targetLength - 1);
		memcpy(out, reply.target, copyLen);
		out[copyLen] = '\0';

		return true;
	}

	auto hardLinkPath(const char *oldPath, const char *newPath) -> bool {
		auto request = VfsLinkMsgData();
		auto reply = VfsLinkReplyMsgData();

		fillName(request.oldPath, sizeof(request.oldPath), request.oldPathLength, oldPath);
		fillName(request.newPath, sizeof(request.newPath), request.newPathLength, newPath);

		return vfsRequest(VFS_LINK_MSG_TYPE, request, reply) and reply.success and reply.status == VFS_STATUS_OK;
	}

	void removeStalePath(const char *path) {
		unlinkPath(path);
	}

	auto cleanupPartitionTestPaths(const char *volume, const char *symlink, const char *hardLink, const char *base, const char *dir) -> bool {
		if (!unlinkPath(symlink)) {
			printf("[VFS test] FAIL partition %s cleanup symlink", volume);
			fflush(stdout);
			return false;
		}

		if (!unlinkPath(hardLink)) {
			printf("[VFS test] FAIL partition %s cleanup hard link", volume);
			fflush(stdout);
			return false;
		}

		if (!unlinkPath(base)) {
			printf("[VFS test] FAIL partition %s cleanup root file", volume);
			fflush(stdout);
			return false;
		}

		if (!unlinkPath(dir)) {
			printf("[VFS test] FAIL partition %s cleanup directory", volume);
			fflush(stdout);
			return false;
		}

		if (!pathMissing(symlink) or !pathMissing(hardLink) or !pathMissing(base) or !pathMissing(dir)) {
			printf("[VFS test] FAIL partition %s cleanup verify", volume);
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto hasDirEntry(const VfsReadDirReplyMsgData &reply, const char *name) -> bool {
		for (uint32_t i = 0; i < reply.entryCount; ++i) {
			const VfsDirEntry &entry = reply.entries[i];

			if (entry.nameLength != 0 and strncmp(entry.name, name, sizeof(entry.name)) == 0) {
				return true;
			}
		}

		return false;
	}

	auto hasHandleDirEntry(const VfsHandleReadDirReplyMsgData &reply, const char *name) -> bool {
		for (uint32_t i = 0; i < reply.entryCount; ++i) {
			const VfsDirEntry &entry = reply.entries[i];

			if (entry.nameLength != 0 and strncmp(entry.name, name, sizeof(entry.name)) == 0) {
				return true;
			}
		}

		return false;
	}

	void sendDeviceReply(const uint64_t destinationPort, hos_msg &replyMsg) {
		replyMsg.port = destinationPort;
		send_horizonos_message(devicePort, destinationPort, &replyMsg);
	}

	auto deviceThreadMain(void */*unused*/) -> void * {
		deviceThreadReady = true;

		alignas(VfsDeviceWriteMsgData) uint8_t requestBuffer[DEVICE_REQUEST_BUFFER_SIZE] {};
		hos_msg msg {};
		msg.buffer = requestBuffer;
		msg.length = sizeof(requestBuffer);

		uint64_t types[] {
			VFS_DEV_READ_MSG_TYPE,
			VFS_DEV_WRITE_MSG_TYPE,
			VFS_DEV_IOCTL_MSG_TYPE,
		};

		filter_options filter {};
		filter.whiteListTypes = types;
		filter.whiteListCount = 3;

		while (deviceRequestsHandled.load() < 3) {
			memset(requestBuffer, 0, sizeof(requestBuffer));

			if (receive_horizonos_message(devicePort, &msg, &filter) != 0) {
				continue;
			}

			if (msg.type == VFS_DEV_READ_MSG_TYPE) {
				const auto *request = reinterpret_cast<VfsDeviceReadMsgData *>(requestBuffer);
				auto reply = VfsDeviceReadReplyMsgData();
				const auto *text = "device-read-ok";
				const size_t textLength = strlen(text);

				reply.success = true;
				reply.status = VFS_STATUS_OK;

				if (request->offset < textLength and request->length != 0) {
					reply.bytesRead = std::min<uint32_t>(request->length, textLength - request->offset);
					memcpy(reply.data, text + request->offset, reply.bytesRead);
				}

				hos_msg replyMsg {};
				replyMsg.type = VFS_DEV_READ_REPLY_MSG_TYPE;
				replyMsg.buffer = &reply;
				replyMsg.length = sizeof(reply);
				sendDeviceReply(msg.src_port, replyMsg);
				deviceRequestsHandled++;
			} else if (msg.type == VFS_DEV_WRITE_MSG_TYPE) {
				auto *request = reinterpret_cast<VfsDeviceWriteMsgData *>(requestBuffer);
				auto reply = VfsDeviceWriteReplyMsgData();

				reply.success = bytesEqual(request->data, request->length, "device-write");
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
				reply.bytesWritten = reply.success ? request->length : 0;

				hos_msg replyMsg {};
				replyMsg.type = VFS_DEV_WRITE_REPLY_MSG_TYPE;
				replyMsg.buffer = &reply;
				replyMsg.length = sizeof(reply);
				sendDeviceReply(msg.src_port, replyMsg);
				deviceRequestsHandled++;
			} else if (msg.type == VFS_DEV_IOCTL_MSG_TYPE) {
				auto *request = reinterpret_cast<VfsDeviceIoctlMsgData *>(requestBuffer);
				auto reply = VfsDeviceIoctlReplyMsgData();

				reply.success = request->request == TEST_IOCTL_REQUEST and bytesEqual(request->input, request->inputLength, "ping");
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					fillBytes(reply.output, sizeof(reply.output), "pong", reply.outputLength);
				}

				hos_msg replyMsg {};
				replyMsg.type = VFS_DEV_IOCTL_REPLY_MSG_TYPE;
				replyMsg.buffer = &reply;
				replyMsg.length = sizeof(reply);
				sendDeviceReply(msg.src_port, replyMsg);
				deviceRequestsHandled++;
			}
		}

		return nullptr;
	}

	auto startDeviceEndpoint() -> bool {
		long port {};

		if (register_horizonos_port(&port, 0) != 0 or port == 0) {
			return false;
		}

		devicePort = static_cast<uint64_t>(port);

		pthread_t thread {};

		if (pthread_create(&thread, nullptr, deviceThreadMain, nullptr) != 0) {
			return false;
		}

		pthread_detach(thread);

		while (!deviceThreadReady.load()) {
			usleep(1000);
		}

		return true;
	}

	auto testMountRefresh() -> bool {
		uint32_t volumeCount {};

		if (!requestMountRefresh(&volumeCount)) {
			return fail("mount refresh", "request failed");
		}

		return expectOk("mount refresh", volumeCount >= 2, "VFS did not report reserved volumes");
	}

	auto testDevicesRoot() -> bool {
		auto statReq = VfsStatMsgData();
		auto statReply = VfsStatReplyMsgData();
		fillName(statReq.path, sizeof(statReq.path), statReq.pathLength, "Devices:");

		if (!vfsRequest(VFS_STAT_MSG_TYPE, statReq, statReply)) {
			return fail("Devices: stat", "request failed");
		}

		if (!expectOk("Devices: stat", statReply.success and statReply.status == VFS_STATUS_OK and statReply.nodeType == VFS_NODE_DIRECTORY, "root is not a directory")) {
			return false;
		}

		auto readDirReq = VfsReadDirMsgData();
		auto readDirReply = VfsReadDirReplyMsgData();
		fillName(readDirReq.path, sizeof(readDirReq.path), readDirReq.pathLength, "Devices:");

		if (!vfsRequest(VFS_READDIR_MSG_TYPE, readDirReq, readDirReply)) {
			return fail("Devices: readdir", "request failed");
		}

		return expectOk("Devices: readdir", readDirReply.success and readDirReply.status == VFS_STATUS_OK and hasDirEntry(readDirReply, "null") and hasDirEntry(readDirReply, "volumes"), "missing null or volumes entry");
	}

	auto testDevicesNull() -> bool {
		auto readReq = VfsReadMsgData();
		auto readReply = VfsReadReplyMsgData();
		fillName(readReq.path, sizeof(readReq.path), readReq.pathLength, "Devices:/null");
		readReq.length = 32;

		if (!vfsRequest(VFS_READ_MSG_TYPE, readReq, readReply)) {
			return fail("Devices:/null read", "request failed");
		}

		if (!expectOk("Devices:/null read", readReply.success and readReply.bytesRead == 0, "null did not return EOF")) {
			return false;
		}

		auto writeReq = VfsWriteMsgData();
		auto writeReply = VfsWriteReplyMsgData();
		fillName(writeReq.path, sizeof(writeReq.path), writeReq.pathLength, "Devices:/null");
		fillBytes(writeReq.data, sizeof(writeReq.data), "discard me", writeReq.length);

		if (!vfsRequest(VFS_WRITE_MSG_TYPE, writeReq, writeReply)) {
			return fail("Devices:/null write", "request failed");
		}

		return expectOk("Devices:/null write", writeReply.success and writeReply.status == VFS_STATUS_OK and writeReply.bytesWritten == writeReq.length, "null did not accept all bytes");
	}

	auto testDirectoryHandle() -> bool {
		auto openReq = VfsOpenMsgData();
		auto openReply = VfsOpenReplyMsgData();
		fillName(openReq.path, sizeof(openReq.path), openReq.pathLength, "Devices:");
		openReq.flags = VFS_OPEN_READ;

		if (!vfsRequest(VFS_OPEN_MSG_TYPE, openReq, openReply)) {
			return fail("directory handle open", "request failed");
		}

		if (!openReply.success or openReply.status != VFS_STATUS_OK or openReply.nodeType != VFS_NODE_DIRECTORY) {
			return fail("directory handle open", "open did not return a directory handle");
		}

		auto readReq = VfsHandleReadDirMsgData();
		auto readReply = VfsHandleReadDirReplyMsgData();
		readReq.handle = openReply.handle;

		if (!vfsRequest(VFS_HANDLE_READDIR_MSG_TYPE, readReq, readReply)) {
			return fail("directory handle readdir", "request failed");
		}

		auto closeReq = VfsCloseMsgData();
		auto closeReply = VfsCloseReplyMsgData();
		closeReq.handle = openReply.handle;
		vfsRequest(VFS_CLOSE_MSG_TYPE, closeReq, closeReply);

		return expectOk("directory handle readdir", readReply.success and readReply.status == VFS_STATUS_OK and hasHandleDirEntry(readReply, "null"), "directory handle did not enumerate Devices:");
	}

	auto registerTestDevice() -> bool {
		auto request = VfsDevRegisterMsgData();
		auto reply = VfsDevRegisterReplyMsgData();
		request.devicePort = devicePort;
		request.permissions = 0666;
		fillName(request.name, sizeof(request.name), request.nameLength, "hworld0");

		if (!vfsRequest(VFS_DEV_REGISTER_MSG_TYPE, request, reply)) {
			return fail("devfs register", "request failed");
		}

		return expectOk("devfs register", reply.success and reply.status == VFS_STATUS_OK, "device node was not registered");
	}

	auto unregisterTestDevice() -> bool {
		auto request = VfsDevUnregisterMsgData();
		auto reply = VfsDevUnregisterReplyMsgData();
		fillName(request.name, sizeof(request.name), request.nameLength, "hworld0");

		if (!vfsRequest(VFS_DEV_UNREGISTER_MSG_TYPE, request, reply)) {
			return fail("devfs unregister", "request failed");
		}

		return expectOk("devfs unregister", reply.success and reply.status == VFS_STATUS_OK, "device node was not unregistered");
	}

	auto testDeviceForwarding() -> bool {
		if (!startDeviceEndpoint()) {
			return fail("devfs endpoint", "could not create fake device port");
		}

		if (!registerTestDevice()) {
			return false;
		}

		auto statReq = VfsStatMsgData();
		auto statReply = VfsStatReplyMsgData();
		fillName(statReq.path, sizeof(statReq.path), statReq.pathLength, "Devices:/hworld0");

		if (!vfsRequest(VFS_STAT_MSG_TYPE, statReq, statReply)) {
			return fail("devfs device stat", "request failed");
		}

		if (!expectOk("devfs device stat", statReply.success and statReply.nodeType == VFS_NODE_DEVICE and statReply.status == VFS_STATUS_OK, "registered node was not visible as a device")) {
			return false;
		}

		auto readReq = VfsReadMsgData();
		auto readReply = VfsReadReplyMsgData();
		fillName(readReq.path, sizeof(readReq.path), readReq.pathLength, "Devices:/hworld0");
		readReq.length = 32;

		if (!vfsRequest(VFS_READ_MSG_TYPE, readReq, readReply)) {
			return fail("devfs device read", "request failed");
		}

		if (!expectOk("devfs device read", readReply.success and bytesEqual(readReply.data, readReply.bytesRead, "device-read-ok"), "read was not forwarded to fake device")) {
			return false;
		}

		auto writeReq = VfsWriteMsgData();
		auto writeReply = VfsWriteReplyMsgData();
		fillName(writeReq.path, sizeof(writeReq.path), writeReq.pathLength, "Devices:/hworld0");
		fillBytes(writeReq.data, sizeof(writeReq.data), "device-write", writeReq.length);

		if (!vfsRequest(VFS_WRITE_MSG_TYPE, writeReq, writeReply)) {
			return fail("devfs device write", "request failed");
		}

		if (!expectOk("devfs device write", writeReply.success and writeReply.status == VFS_STATUS_OK and writeReply.bytesWritten == writeReq.length, "write was not forwarded to fake device")) {
			return false;
		}

		auto openReq = VfsOpenMsgData();
		auto openReply = VfsOpenReplyMsgData();
		fillName(openReq.path, sizeof(openReq.path), openReq.pathLength, "Devices:/hworld0");
		openReq.flags = VFS_OPEN_READ | VFS_OPEN_WRITE;

		if (!vfsRequest(VFS_OPEN_MSG_TYPE, openReq, openReply)) {
			return fail("devfs device open", "request failed");
		}

		if (!openReply.success or openReply.nodeType != VFS_NODE_DEVICE) {
			return fail("devfs device open", "open did not return a device handle");
		}

		auto ioctlReq = VfsIoctlMsgData();
		auto ioctlReply = VfsIoctlReplyMsgData();
		ioctlReq.handle = openReply.handle;
		ioctlReq.request = TEST_IOCTL_REQUEST;
		fillBytes(ioctlReq.input, sizeof(ioctlReq.input), "ping", ioctlReq.inputLength);

		if (!vfsRequest(VFS_IOCTL_MSG_TYPE, ioctlReq, ioctlReply)) {
			return fail("devfs device ioctl", "request failed");
		}

		auto closeReq = VfsCloseMsgData();
		auto closeReply = VfsCloseReplyMsgData();
		closeReq.handle = openReply.handle;
		vfsRequest(VFS_CLOSE_MSG_TYPE, closeReq, closeReply);

		if (!expectOk("devfs device ioctl", ioctlReply.success and ioctlReply.status == VFS_STATUS_OK and bytesEqual(ioctlReply.output, ioctlReply.outputLength, "pong"), "ioctl was not forwarded to fake device")) {
			return false;
		}

		return unregisterTestDevice();
	}

	void parseMountedPartitionVolumes(const char *text, const uint32_t textLength, VolumeList &out) {
		size_t lineStart = 0;
		out.count = 0;

		while (lineStart < textLength and out.count < 16) {
			size_t lineEnd = lineStart;

			while (lineEnd < textLength and text[lineEnd] != '\n') {
				++lineEnd;
			}

			size_t firstSpace = lineStart;

			while (firstSpace < lineEnd and text[firstSpace] != ' ') {
				++firstSpace;
			}

			if (firstSpace < lineEnd) {
				char volume[VFS_MAX_NAME_LENGTH] {};
				const size_t volumeLen = std::min(sizeof(volume) - 1, firstSpace - lineStart);
				memcpy(volume, text + lineStart, volumeLen);
				volume[volumeLen] = '\0';

				const bool reserved = strcmp(volume, "Devices:") == 0 or strcmp(volume, "Tmp:") == 0;
				bool mounted = false;

				for (size_t i = firstSpace; i + 9 <= lineEnd; ++i) {
					if (memcmp(text + i, " mounted ", 9) == 0) {
						mounted = true;
						break;
					}
				}

				if (!reserved and mounted and volume[0] != '\0') {
					memcpy(out.names[out.count], volume, sizeof(out.names[out.count]));
					++out.count;
				}
			}

			if (lineEnd >= textLength) {
				break;
			}

			lineStart = lineEnd + 1;
		}
	}

	auto readVolumesText(char *text, const size_t textSize, uint32_t &textLength) -> bool {
		return readPath("Devices:/volumes", text, textSize, textLength);
	}

	auto mountedPartitionVolumes(VolumeList &out, char *text = nullptr, const size_t textSize = 0, uint32_t *textLengthOut = nullptr) -> bool {
		char localText[VFS_MAX_READ_SIZE + 1] {};
		uint32_t textLength {};
		char *textBuffer = text == nullptr ? localText : text;
		const size_t bufferSize = text == nullptr ? sizeof(localText) : textSize;

		if (!readVolumesText(textBuffer, bufferSize, textLength)) {
			return false;
		}

		parseMountedPartitionVolumes(textBuffer, textLength, out);

		if (textLengthOut != nullptr) {
			*textLengthOut = textLength;
		}

		return true;
	}

	auto testPartitionRootFileLinks(const char *volume) -> bool {
		char base[VFS_MAX_PATH_LENGTH] {};
		char dir[VFS_MAX_PATH_LENGTH] {};
		char symlink[VFS_MAX_PATH_LENGTH] {};
		char hardLink[VFS_MAX_PATH_LENGTH] {};
		const char *payload = "hello from HorizonOS VFS partition test";
		const char *symlinkTarget = "/vfs_test_root_file.txt";

		snprintf(base, sizeof(base), "%s/vfs_test_root_file.txt", volume);
		snprintf(dir, sizeof(dir), "%s/vfs_test_links", volume);
		snprintf(symlink, sizeof(symlink), "%s/root_file.sym", dir);
		snprintf(hardLink, sizeof(hardLink), "%s/root_file.hard", dir);

		removeStalePath(symlink);
		removeStalePath(hardLink);
		removeStalePath(base);
		removeStalePath(dir);

		if (!createFile(base)) {
			printf("[VFS test] FAIL partition %s root file create", volume);
			fflush(stdout);
			return false;
		}

		if (!writePath(base, payload)) {
			printf("[VFS test] FAIL partition %s root file write", volume);
			fflush(stdout);
			return false;
		}

		char readBack[VFS_MAX_READ_SIZE + 1] {};
		uint32_t readBackLength {};

		if (!readPath(base, readBack, sizeof(readBack), readBackLength) or strcmp(readBack, payload) != 0) {
			printf("[VFS test] FAIL partition %s root file readback", volume);
			fflush(stdout);
			return false;
		}

		if (!mkdirPath(dir)) {
			printf("[VFS test] FAIL partition %s mkdir", volume);
			fflush(stdout);
			return false;
		}

		if (!symlinkPath(symlinkTarget, symlink)) {
			printf("[VFS test] FAIL partition %s symlink create", volume);
			fflush(stdout);
			return false;
		}

		char linkTarget[VFS_MAX_PATH_LENGTH] {};

		if (!readLinkPath(symlink, linkTarget, sizeof(linkTarget)) or strcmp(linkTarget, symlinkTarget) != 0) {
			printf("[VFS test] FAIL partition %s symlink verify", volume);
			fflush(stdout);
			return false;
		}

		if (!hardLinkPath(base, hardLink)) {
			printf("[VFS test] FAIL partition %s hard link create", volume);
			fflush(stdout);
			return false;
		}

		char hardReadBack[VFS_MAX_READ_SIZE + 1] {};
		uint32_t hardReadBackLength {};

		if (!readPath(hardLink, hardReadBack, sizeof(hardReadBack), hardReadBackLength) or strcmp(hardReadBack, payload) != 0) {
			printf("[VFS test] FAIL partition %s hard link readback", volume);
			fflush(stdout);
			return false;
		}

		if (!cleanupPartitionTestPaths(volume, symlink, hardLink, base, dir)) {
			return false;
		}

		printf("[VFS test] PASS partition %s root file/link workflow", volume);
		fflush(stdout);

		return true;
	}

	auto testPartitionFileWorkflows() -> bool {
		constexpr uint32_t PARTITION_MOUNT_WAIT_ATTEMPTS = 300;
		constexpr useconds_t PARTITION_MOUNT_WAIT_US = 100000;

		printf("[VFS test] starting partition file workflows");
		fflush(stdout);

		VolumeList volumes {};
		char volumeText[VFS_MAX_READ_SIZE + 1] {};
		uint32_t volumeTextLength {};
		bool couldReadVolumes = false;

		for (uint32_t attempt = 0; attempt < PARTITION_MOUNT_WAIT_ATTEMPTS; ++attempt) {
			requestMountRefresh();
			memset(volumeText, 0, sizeof(volumeText));
			volumeTextLength = 0;
			couldReadVolumes = mountedPartitionVolumes(volumes, volumeText, sizeof(volumeText), &volumeTextLength);

			if (couldReadVolumes and volumes.count != 0) {
				break;
			}

			if (attempt == 0 or (attempt + 1) % 50 == 0) {
				printf("[VFS test] waiting for mounted partition volumes (%u/%u)", attempt + 1, PARTITION_MOUNT_WAIT_ATTEMPTS);
				fflush(stdout);
			}

			usleep(PARTITION_MOUNT_WAIT_US);
		}

		if (!couldReadVolumes) {
			return fail("partition file workflows", "could not read Devices:/volumes");
		}

		if (volumes.count == 0) {
			for (uint32_t i = 0; i < volumeTextLength; ++i) {
				if (volumeText[i] == '\n' or volumeText[i] == '\r') {
					volumeText[i] = '|';
				}
			}

			printf("[VFS test] Devices:/volumes after wait: %.*s", static_cast<int>(volumeTextLength), volumeText);
			fflush(stdout);
			return fail("partition file workflows", "no mounted partition volumes found");
		}

		bool ok = true;

		for (uint32_t i = 0; i < volumes.count; ++i) {
			ok = testPartitionRootFileLinks(volumes.names[i]) and ok;
		}

		return ok;
	}
}

auto runVfsTests() -> bool {
	printf("[VFS test] starting");
	fflush(stdout);

	bool ok = true;
	ok = testMountRefresh() and ok;
	ok = testDevicesRoot() and ok;
	ok = testDevicesNull() and ok;
	ok = testDirectoryHandle() and ok;
	ok = testDeviceForwarding() and ok;
	ok = testPartitionFileWorkflows() and ok;

	printf("[VFS test] %s", ok ? "all passed" : "failed");
	fflush(stdout);

	return ok;
}
