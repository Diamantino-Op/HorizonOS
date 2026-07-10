#include "Fat32.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

Fat32Service service;

namespace {
	constexpr uint32_t FILESYSTEM_STORAGE_ATTEMPTS = 5;
	constexpr useconds_t FILESYSTEM_STORAGE_RETRY_DELAY_US = 100000;

	template<typename Reply>
	auto receiveStorageReply(const uint64_t replyType, const uint64_t requestId, Reply &reply) -> int {
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		uint64_t acceptedType = replyType;
		auto filter = filter_options();
		filter.whiteListTypes = &acceptedType;
		filter.whiteListCount = 1;

		for (;;) {
			reply = {};
			const int ret = receive_horizonos_message(service.storageReplyPort, &recv, &filter);

			// Message delivery can transiently lose the current process mapping
			// while the scheduler switches address spaces. The request remains in
			// flight, so retry the receive instead of turning it into disk I/O loss.
			if (ret == EFAULT) {
				usleep(1000);
				continue;
			}

			if (ret != 0 or reply.requestId == requestId) {
				return ret;
			}
		}
	}
}

auto Fat32Utils::allocateStorageRequestId() -> uint64_t {
	return service.allocateStorageRequestId();
}

	void Fat32Utils::fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());

		memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto Fat32Utils::validName(const char *name, const size_t length, const size_t maxLength, string &out) -> bool {
		if (length == 0 or length > maxLength or name[length - 1] != '\0') {
			return false;
		}

		out.assign(name, length - 1);

		return true;
	}

	auto Fat32Utils::validPath(const char *path, const size_t length, string &out) -> bool {
		return Fat32Utils::validName(path, length, VFS_MAX_PATH_LENGTH, out);
	}

	auto Fat32Utils::isPowerOfTwo(const uint32_t value) -> bool {
		return value != 0 and (value & (value - 1)) == 0;
	}

	auto Fat32Utils::upperAscii(string value) -> string {
		for (char &ch : value) {
			ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
		}

		return value;
	}

	auto Fat32Utils::pathParts(const string &path) -> vector<string> {
		vector<string> parts;
		size_t start = 0;

		while (start < path.size()) {
			while (start < path.size() and path[start] == '/') {
				++start;
			}

			const size_t end = path.find('/', start);
			const size_t partEnd = end == string::npos ? path.size() : end;

			if (partEnd > start) {
				parts.push_back(path.substr(start, partEnd - start));
			}

			if (end == string::npos) {
				break;
			}

			start = end + 1;
		}

		return parts;
	}

	auto Fat32Utils::registerWithNameRegistry(const char *name) -> bool {
		auto msg = hos_msg();
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		Fat32Utils::fillName(data.name, sizeof(data.name), data.nameLength, name);

		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.fat32Port, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto replyMsg = hos_msg();

		replyMsg.buffer = &reply;
		replyMsg.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.fat32Port, &replyMsg, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Fat32Utils::waitForStorage() -> uint64_t {
		for (;;) {
			auto check = CheckMsgData();

			Fat32Utils::fillName(check.name, sizeof(check.name), check.nameLength, "StorageManager");

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(service.fat32Port, 1, &checkMsg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(service.fat32Port, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and reply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();

		Fat32Utils::fillName(get.name, sizeof(get.name), get.nameLength, "StorageManager");

		auto getMsg = hos_msg();

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &get;
		getMsg.length = sizeof(get);

		send_horizonos_message(service.fat32Port, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(service.fat32Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.port;
	}

	auto Fat32Utils::waitForServicePort(const char *name) -> uint64_t {
		for (;;) {
			auto check = CheckMsgData();

			Fat32Utils::fillName(check.name, sizeof(check.name), check.nameLength, name);

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(service.fat32Port, 1, &checkMsg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(service.fat32Port, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and reply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();

		Fat32Utils::fillName(get.name, sizeof(get.name), get.nameLength, name);

		auto getMsg = hos_msg();

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &get;
		getMsg.length = sizeof(get);

		send_horizonos_message(service.fat32Port, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(service.fat32Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.port;
	}

	auto Fat32Utils::registerWithVfs(const char *fsName) -> bool {
		const uint64_t vfsPort = Fat32Utils::waitForServicePort("Vfs");

		if (vfsPort == 0) {
			return false;
		}

		auto data = VfsRegisterFsHandlerMsgData();

		data.handlerPort = service.fat32Port;
		Fat32Utils::fillName(data.fsName, sizeof(data.fsName), data.fsNameLength, fsName);

		auto msg = hos_msg();

		msg.type = VFS_REGISTER_FS_HANDLER_MSG_TYPE;
		msg.port = vfsPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.fat32Port, vfsPort, &msg) != 0) {
			return false;
		}

		auto reply = VfsRegisterFsHandlerReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_REGISTER_FS_HANDLER_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.fat32Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Fat32Utils::registerFsHandler() -> bool {
		auto data = StorageRegisterFsHandlerMsgData();

		data.handlerPort = service.fat32Port;
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		Fat32Utils::fillName(data.fsName, sizeof(data.fsName), data.fsNameLength, "fat32");

		auto msg = hos_msg();

		msg.type = STORAGE_REGISTER_FS_HANDLER_MSG_TYPE;
		msg.port = service.storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.fat32Port, service.storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageRegisterFsHandlerReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_FS_HANDLER_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.fat32Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Fat32Utils::readDevicePage(const uint64_t deviceId, const uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool {
		if (allocPhysPage(&phys) != 0) {
			return false;
		}

		if (mmap_phys(phys, 0x1000, &virt, false) != 0) {
			freePhysPage(phys);

			phys = 0;

			return false;
		}

		ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			memset(reinterpret_cast<void *>(virt), 0, 0x1000);
			const uint64_t requestId = Fat32Utils::allocateStorageRequestId();
			auto data = StorageReadMsgData();

			data.replyPort = service.storageReplyPort;
			data.requestId = requestId;
			data.deviceId = deviceId;
			data.lba = lba;
			data.pageCount = 1;
			data.pagePhysArray[0] = phys;

			auto msg = hos_msg();

			msg.type = STORAGE_READ_MSG_TYPE;
			msg.port = service.storagePort;
			msg.buffer = &data;
			msg.length = sizeof(data);

			if (send_horizonos_message(service.storageReplyPort, service.storagePort, &msg) == 0) {
				auto reply = StorageReadReplyMsgData();
				const int ret = receiveStorageReply(STORAGE_READ_REPLY_MSG_TYPE, requestId, reply);

				if (ret == 0 and reply.success) {
					return true;
				}
			}

			if (attempt + 1 < FILESYSTEM_STORAGE_ATTEMPTS) {
				usleep(FILESYSTEM_STORAGE_RETRY_DELAY_US);
			}
		}

		munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		freePhysPage(phys);
		phys = 0;
		virt = 0;
		return false;
	}

	void Fat32Utils::freeDevicePage(uint64_t &phys, uint64_t &virt) {
		if (virt != 0) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
			virt = 0;
		}

		if (phys != 0) {
			freePhysPage(phys);
			phys = 0;
		}
	}

	auto Fat32Utils::writeDevicePage(const uint64_t deviceId, const uint64_t lba, const uint64_t phys) -> bool {
		ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			const uint64_t requestId = Fat32Utils::allocateStorageRequestId();
			auto data = StorageWriteMsgData();

			data.replyPort = service.storageReplyPort;
			data.requestId = requestId;
			data.deviceId = deviceId;
			data.lba = lba;
			data.pageCount = 1;
			data.pagePhysArray[0] = phys;

			auto msg = hos_msg();

			msg.type = STORAGE_WRITE_MSG_TYPE;
			msg.port = service.storagePort;
			msg.buffer = &data;
			msg.length = sizeof(data);

			if (send_horizonos_message(service.storageReplyPort, service.storagePort, &msg) == 0) {
				auto reply = StorageWriteReplyMsgData();
				const int ret = receiveStorageReply(STORAGE_WRITE_REPLY_MSG_TYPE, requestId, reply);

				if (ret == 0 and reply.success) {
					return true;
				}
			}

			if (attempt + 1 < FILESYSTEM_STORAGE_ATTEMPTS) {
				usleep(FILESYSTEM_STORAGE_RETRY_DELAY_US);
			}
		}

		return false;
	}

	auto Fat32Utils::flushDevice(const uint64_t deviceId) -> bool {
		ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			const uint64_t requestId = Fat32Utils::allocateStorageRequestId();
			auto data = StorageFlushMsgData();

			data.replyPort = service.storageReplyPort;
			data.requestId = requestId;
			data.deviceId = deviceId;

			auto msg = hos_msg();

			msg.type = STORAGE_FLUSH_MSG_TYPE;
			msg.port = service.storagePort;
			msg.buffer = &data;
			msg.length = sizeof(data);

			if (send_horizonos_message(service.storageReplyPort, service.storagePort, &msg) == 0) {
				auto reply = StorageFlushReplyMsgData();
				const int ret = receiveStorageReply(STORAGE_FLUSH_REPLY_MSG_TYPE, requestId, reply);

				if (ret == 0 and reply.success) {
					return true;
				}
			}

			if (attempt + 1 < FILESYSTEM_STORAGE_ATTEMPTS) {
				usleep(FILESYSTEM_STORAGE_RETRY_DELAY_US);
			}
		}

		return false;
	}



auto Fat32Utils::probeFat32(const StorageFsProbeDeviceMsgData &device) -> bool {
	Fat32Volume volume(device);

	if (!volume.load()) {
		return false;
	}

	printf("FAT32: Recognized %s id=%lu clusters=%lu blockSize=%u.", device.deviceName, device.deviceId, volume.getClusterCount(), device.blockSize);
	fflush(stdout);

	return true;
}

namespace {
	template<typename Reply>
	void sendReply(const uint64_t type, const hos_msg &request, Reply &reply) {
		auto replyMsg = hos_msg();

		replyMsg.type = type;
		replyMsg.port = request.src_port;
		replyMsg.buffer = &reply;
		replyMsg.length = sizeof(reply);

		send_horizonos_message(service.fat32Port, request.src_port, &replyMsg);
	}

	template<typename Data, typename Reply>
	[[noreturn]] auto readOnlyHandler(const uint64_t requestType, const uint64_t replyType, const uint32_t status) -> void * {
		auto data = Data();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { requestType };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = Reply();
			reply.success = false;
			reply.status = status;
			sendReply(replyType, msg, reply);
		}
	}

	[[noreturn]] auto mountHandler(void */*unused*/) -> void * {
		auto data = FsMountMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_MOUNT_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMountReplyMsgData();
			string deviceName;

			if (data.deviceId != 0 and data.blockCount != 0 and data.blockSize != 0 and Fat32Utils::validName(data.deviceName, data.deviceNameLength, sizeof(data.deviceName), deviceName)) {
				StorageFsProbeDeviceMsgData device {};

				device.deviceId = data.deviceId;
				device.blockCount = data.blockCount;
				device.blockSize = data.blockSize;
				Fat32Utils::fillName(device.deviceName, sizeof(device.deviceName), device.deviceNameLength, deviceName);

				Fat32Volume volume(device);

				if (volume.load()) {
					ScopedMutex lock(service.mountsLock);
					const uint64_t mountId = service.nextMountId++;

					service.mounts.push_back(MountedFat32 { .mountId = mountId, .device = device });
					reply.success = true;
					reply.mountId = mountId;

					printf("FAT32: Mounted %s as mount %lu.", device.deviceName, mountId);
					fflush(stdout);
				}
			}

			sendReply(FS_MOUNT_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto statHandler(void */*unused*/) -> void * {
		auto data = FsStatMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_STAT_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsStatReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				FatDirEntry entry {};

				if (volume.load() and volume.lookupPath(path, entry)) {
					reply.success = true;
					reply.nodeType = volume.nodeType(entry);
					reply.size = volume.size(entry);
					reply.status = VFS_STATUS_OK;
					reply.nodeId = volume.nodeId(entry);
				} else {
					reply.status = volume.hadIoFailure() ? VFS_STATUS_IO_ERROR : VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_STAT_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto readDirHandler(void */*unused*/) -> void * {
		auto data = FsReadDirMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_READDIR_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadDirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				FatDirEntry entry {};
				vector<VfsDirEntry> entries;
				bool hasMore = false;
				uint32_t nextOffset = data.offset;

				if (volume.load() and volume.lookupPath(path, entry) and volume.readDirectory(entry, entries, data.offset, &hasMore, &nextOffset)) {
					reply.success = true;
					reply.entryCount = min<uint32_t>(entries.size(), VFS_MAX_DIR_ENTRIES);
					reply.status = VFS_STATUS_OK;
					reply.nextOffset = nextOffset;
					reply.hasMore = hasMore;

					for (uint32_t i = 0; i < reply.entryCount; ++i) {
						reply.entries[i] = entries[i];
					}
				} else {
					reply.status = volume.hadIoFailure() ? VFS_STATUS_IO_ERROR : VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_READDIR_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto readHandler(void */*unused*/) -> void * {
		auto data = FsReadMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_READ_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				FatDirEntry entry {};
				vector<uint8_t> bytes;

				if (volume.load() and volume.lookupPath(path, entry) and volume.nodeType(entry) == VFS_NODE_FILE and volume.readFileRange(entry, data.offset, data.length, bytes)) {
					reply.bytesRead = bytes.size();
					memcpy(reply.data, bytes.data(), reply.bytesRead);
					reply.success = true;
				}
			}

			sendReply(FS_READ_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto syncHandler(void */*unused*/) -> void * {
		auto data = FsSyncMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_SYNC_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsSyncReplyMsgData();
			StorageFsProbeDeviceMsgData device {};

			if (service.mountedDevice(data.mountId, device)) {
				reply.success = Fat32Utils::flushDevice(device.deviceId);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
			} else {
				reply.status = VFS_STATUS_NOT_FOUND;
			}

			sendReply(FS_SYNC_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto probeHandler(void */*unused*/) -> void * {
		auto data = StorageFsProbeDeviceMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_FS_PROBE_DEVICE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageFsProbeDeviceReplyMsgData();
			reply.recognized = Fat32Utils::probeFat32(data);

			sendReply(STORAGE_FS_PROBE_DEVICE_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto writeHandler(void */*unused*/) -> void * {
		auto data = FsWriteMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_WRITE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsWriteReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				uint64_t size = 0;

				if (volume.load() and volume.writeFile(path, data.offset, data.data, data.length, size)) {
					reply.success = true;
					reply.bytesWritten = data.length;
					reply.size = size;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = volume.hadIoFailure() ? VFS_STATUS_IO_ERROR : VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_WRITE_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto createHandler(void */*unused*/) -> void * {
		auto data = FsCreateMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_CREATE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsCreateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.nodeType == VFS_NODE_FILE and service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);

				if (volume.load() and volume.createFile(path, reply.nodeId)) {
					reply.success = true;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_CREATE_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto unlinkHandler(void */*unused*/) -> void * {
		auto data = FsUnlinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_UNLINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsUnlinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				reply.success = volume.load() and volume.unlinkFile(path);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_UNLINK_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto renameHandler(void */*unused*/) -> void * {
		auto data = FsRenameMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_RENAME_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsRenameReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string oldPath;
			string newPath;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.oldPath, data.oldPathLength, oldPath) and Fat32Utils::validPath(data.newPath, data.newPathLength, newPath)) {
				Fat32Volume volume(device);
				reply.success = volume.load() and volume.renameFile(oldPath, newPath);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_RENAME_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto truncateHandler(void */*unused*/) -> void * {
		auto data = FsTruncateMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_TRUNCATE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsTruncateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);
				uint64_t size = 0;

				if (volume.load() and volume.truncateFile(path, data.size, size)) {
					reply.success = true;
					reply.size = size;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_TRUNCATE_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto mkdirHandler(void */*unused*/) -> void * {
		auto data = FsMkdirMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_MKDIR_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.fat32Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMkdirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Fat32Utils::validPath(data.path, data.pathLength, path)) {
				Fat32Volume volume(device);

				if (volume.load() and volume.createDirectory(path, reply.nodeId)) {
					reply.success = true;
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			sendReply(FS_MKDIR_REPLY_MSG_TYPE, msg, reply);
		}
	}

	[[noreturn]] auto linkHandler(void */*unused*/) -> void * {
		readOnlyHandler<FsLinkMsgData, FsLinkReplyMsgData>(FS_LINK_MSG_TYPE, FS_LINK_REPLY_MSG_TYPE, VFS_STATUS_READ_ONLY);
	}

	[[noreturn]] auto symlinkHandler(void */*unused*/) -> void * {
		readOnlyHandler<FsSymlinkMsgData, FsSymlinkReplyMsgData>(FS_SYMLINK_MSG_TYPE, FS_SYMLINK_REPLY_MSG_TYPE, VFS_STATUS_READ_ONLY);
	}

	[[noreturn]] auto readlinkHandler(void */*unused*/) -> void * {
		readOnlyHandler<FsReadLinkMsgData, FsReadLinkReplyMsgData>(FS_READLINK_MSG_TYPE, FS_READLINK_REPLY_MSG_TYPE, VFS_STATUS_UNSUPPORTED);
	}
}

auto Fat32Service::start() -> int {
	if (register_horizonos_port(reinterpret_cast<long *>(&service.fat32Port)) != 0) {
		printf("FAT32: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!Fat32Utils::registerWithNameRegistry("Fat32")) {
		printf("FAT32: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	service.storagePort = Fat32Utils::waitForStorage();

	if (register_horizonos_port(reinterpret_cast<long *>(&service.storageReplyPort)) != 0 or service.storageReplyPort == 0) {
		printf("FAT32: Failed to register Storage reply port.");
		fflush(stdout);

		return 1;
	}

	if (!Fat32Utils::registerFsHandler()) {
		printf("FAT32: Failed to register filesystem handler.");
		fflush(stdout);

		return 1;
	}

	if (!Fat32Utils::registerWithVfs("fat32")) {
		printf("FAT32: Failed to register with VFS.");
		fflush(stdout);

		return 1;
	}

	printf("FAT32: Registered handler on port %lu with Storage port %lu.", service.fat32Port, service.storagePort);
	fflush(stdout);

	pthread_t probeThread;
	pthread_t mountThread;
	pthread_t statThread;
	pthread_t readDirThread;
	pthread_t readThread;
	pthread_t writeThread;
	pthread_t createThread;
	pthread_t mkdirThread;
	pthread_t unlinkThread;
	pthread_t renameThread;
	pthread_t truncateThread;
	pthread_t syncThread;
	pthread_t linkThread;
	pthread_t symlinkThread;
	pthread_t readlinkThread;

	if (pthread_create(&probeThread, nullptr, probeHandler, nullptr) != 0 or
	    pthread_create(&mountThread, nullptr, mountHandler, nullptr) != 0 or
	    pthread_create(&statThread, nullptr, statHandler, nullptr) != 0 or
	    pthread_create(&readDirThread, nullptr, readDirHandler, nullptr) != 0 or
	    pthread_create(&readThread, nullptr, readHandler, nullptr) != 0 or
	    pthread_create(&writeThread, nullptr, writeHandler, nullptr) != 0 or
	    pthread_create(&createThread, nullptr, createHandler, nullptr) != 0 or
	    pthread_create(&mkdirThread, nullptr, mkdirHandler, nullptr) != 0 or
	    pthread_create(&syncThread, nullptr, syncHandler, nullptr) != 0 or
	    pthread_create(&linkThread, nullptr, linkHandler, nullptr) != 0 or
	    pthread_create(&symlinkThread, nullptr, symlinkHandler, nullptr) != 0 or
	    pthread_create(&readlinkThread, nullptr, readlinkHandler, nullptr) != 0 or
	    pthread_create(&unlinkThread, nullptr, unlinkHandler, nullptr) != 0 or
	    pthread_create(&renameThread, nullptr, renameHandler, nullptr) != 0 or
	    pthread_create(&truncateThread, nullptr, truncateHandler, nullptr) != 0) {
		printf("FAT32: Failed to create message handlers.");
		fflush(stdout);

		return 1;
	}

	pthread_detach(probeThread);
	pthread_detach(mountThread);
	pthread_detach(statThread);
	pthread_detach(readDirThread);
	pthread_detach(readThread);
	pthread_detach(writeThread);
	pthread_detach(createThread);
	pthread_detach(mkdirThread);
	pthread_detach(syncThread);
	pthread_detach(linkThread);
	pthread_detach(symlinkThread);
	pthread_detach(readlinkThread);
	pthread_detach(unlinkThread);
	pthread_detach(renameThread);
	pthread_detach(truncateThread);

	for (;;) {
		usleep(100000);
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	return service.start();
}
