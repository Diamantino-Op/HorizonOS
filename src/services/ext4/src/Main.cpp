#include "Ext4.hpp"
#include "Service.hpp"
#include "StorageProtocol.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;
using namespace horizonos::services::ext4;

namespace horizonos::services::ext4 {
	Ext4Service service;
}

namespace {
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMountReplyMsgData();
			string deviceName;

			if (data.deviceId != 0 and data.blockCount != 0 and data.blockSize != 0 and Utils::validName(data.deviceName, data.deviceNameLength, sizeof(data.deviceName), deviceName)) {
				StorageFsProbeDeviceMsgData device {};

				device.deviceId = data.deviceId;
				device.blockCount = data.blockCount;
				device.blockSize = data.blockSize;
				Utils::fillName(device.deviceName, sizeof(device.deviceName), device.deviceNameLength, deviceName);

				Ext4Volume volume(device);

				if (volume.load()) {
					ScopedMutex lock(service.mountsLock);
					const uint64_t mountId = service.nextMountId++;

					service.mounts.push_back(MountedExt4 { .mountId = mountId, .device = device });
					reply.success = true;
					reply.mountId = mountId;

					printf("Ext4: Mounted %s as mount %lu.", device.deviceName, mountId);
					fflush(stdout);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_MOUNT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsStatReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);
				Ext4Inode inode {};
				uint32_t inodeNumber = 0;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode)) {
					reply.success = true;
					reply.nodeType = Utils::inodeNodeType(inode);
					reply.size = volume.fileSize(inode);
					reply.status = VFS_STATUS_OK;
					reply.nodeId = inodeNumber;
				} else {
					reply.status = volume.hadIoFailure() ? VFS_STATUS_IO_ERROR : VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_STAT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadDirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);
				Ext4Inode inode {};
				uint32_t inodeNumber = 0;
				vector<VfsDirEntry> entries;
				bool hasMore = false;
				uint32_t nextOffset = data.offset;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode) and volume.readDirectory(inode, entries, data.offset, &hasMore, &nextOffset)) {
					(void) inodeNumber;
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

			auto replyMsg = hos_msg();

			replyMsg.type = FS_READDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);
				Ext4Inode inode {};
				uint32_t inodeNumber = 0;
				vector<uint8_t> bytes;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode) and Utils::inodeNodeType(inode) == VFS_NODE_FILE and volume.readFileRange(inode, data.offset, data.length, bytes)) {
					(void) inodeNumber;
					reply.bytesRead = bytes.size();
					memcpy(reply.data, bytes.data(), reply.bytesRead);
					reply.success = true;
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_READ_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsWriteReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);
				Ext4Inode inode {};
				uint32_t inodeNumber = 0;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode) and Utils::inodeNodeType(inode) == VFS_NODE_FILE and volume.writeFile(inodeNumber, inode, data.offset, data.data, data.length)) {
					reply.success = true;
					reply.bytesWritten = data.length;
					reply.size = volume.fileSize(inode);
					reply.status = VFS_STATUS_OK;
				} else {
					reply.status = VFS_STATUS_INVALID;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_WRITE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsCreateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.nodeType == VFS_NODE_FILE and service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.createFile(path);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext4Inode inode {};
					uint32_t inodeNumber = 0;

					if (volume.lookupPath(path, inodeNumber, inode)) {
						reply.nodeId = inodeNumber;
					}
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_CREATE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMkdirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.createDirectory(path);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext4Inode inode {};
					uint32_t inodeNumber = 0;

					if (volume.lookupPath(path, inodeNumber, inode)) {
						reply.nodeId = inodeNumber;
					}
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_MKDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsSyncReplyMsgData();
			StorageFsProbeDeviceMsgData device {};

			if (service.mountedDevice(data.mountId, device)) {
				reply.success = Utils::flushDevice(device.deviceId);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
			} else {
				reply.status = VFS_STATUS_NOT_FOUND;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_SYNC_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto linkHandler(void */*unused*/) -> void * {
		auto data = FsLinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_LINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsLinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string oldPath;
			string newPath;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.oldPath, data.oldPathLength, oldPath) and Utils::validPath(data.newPath, data.newPathLength, newPath)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.createHardLink(oldPath, newPath);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext4Inode inode {};
					uint32_t inodeNumber = 0;

					if (volume.lookupPath(newPath, inodeNumber, inode)) {
						reply.nodeId = inodeNumber;
					}
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_LINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto symlinkHandler(void */*unused*/) -> void * {
		auto data = FsSymlinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_SYMLINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsSymlinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string target;
			string linkPath;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.target, data.targetLength, target) and Utils::validPath(data.linkPath, data.linkPathLength, linkPath)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.createSymlink(target, linkPath);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext4Inode inode {};
					uint32_t inodeNumber = 0;

					if (volume.lookupPath(linkPath, inodeNumber, inode)) {
						reply.nodeId = inodeNumber;
					}
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_SYMLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto readlinkHandler(void */*unused*/) -> void * {
		auto data = FsReadLinkMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { FS_READLINK_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadLinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;
			string target;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.readSymlink(path, target);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Utils::fillName(reply.target, sizeof(reply.target), reply.targetLength, target);
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_READLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsUnlinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.unlinkFile(path);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_UNLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsRenameReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string oldPath;
			string newPath;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.oldPath, data.oldPathLength, oldPath) and Utils::validPath(data.newPath, data.newPathLength, newPath)) {
				Ext4Volume volume(device);

				reply.success = volume.load() and volume.renameFile(oldPath, newPath);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_RENAME_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsTruncateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (service.mountedDevice(data.mountId, device) and Utils::validPath(data.path, data.pathLength, path)) {
				Ext4Volume volume(device);

				if (volume.load() and volume.truncateFile(path, data.size)) {
					reply.success = true;
					reply.size = data.size;
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_TRUNCATE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(service.ext4Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageFsProbeDeviceReplyMsgData();
			reply.recognized = Utils::probeExt4(data);

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_FS_PROBE_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			
			send_horizonos_message(service.ext4Port, msg.src_port, &replyMsg);
		}
	}
}

auto Ext4Service::start() -> int {
	if (register_horizonos_port(reinterpret_cast<long *>(&service.ext4Port)) != 0) {
		printf("Ext4: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!Utils::registerWithNameRegistry("Ext4")) {
		printf("Ext4: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	service.storagePort = Utils::waitForStorage();

	if (register_horizonos_port(reinterpret_cast<long *>(&service.storageReplyPort)) != 0 or service.storageReplyPort == 0) {
		printf("Ext4: Failed to register Storage reply port.");
		fflush(stdout);

		return 1;
	}

	if (!Utils::registerFsHandler()) {
		printf("Ext4: Failed to register filesystem handler.");
		fflush(stdout);

		return 1;
	}

	if (!Utils::registerWithVfs("ext4")) {
		printf("Ext4: Failed to register with VFS.");
		fflush(stdout);

		return 1;
	}

	printf("Ext4: Registered handler on port %lu with Storage port %lu.", service.ext4Port, service.storagePort);
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
		printf("Ext4: Failed to create message handlers.");
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
