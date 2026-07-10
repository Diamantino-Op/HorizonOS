#include "Service.hpp"
#include "Ext4.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace horizonos::services::ext4 {
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

	void Utils::fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());

		std::memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto Utils::validName(const char *name, const size_t length, const size_t maxLength, string &out) -> bool {
		if (length == 0 or length > maxLength or name[length - 1] != '\0') {
			return false;
		}

		out.assign(name, length - 1);

		return true;
	}

	auto Utils::registerWithNameRegistry(const char *name) -> bool {
		auto msg = hos_msg();
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		fillName(data.name, sizeof(data.name), data.nameLength, name);

		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.ext4Port, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto replyMsg = hos_msg();

		replyMsg.buffer = &reply;
		replyMsg.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.ext4Port, &replyMsg, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Utils::waitForStorage() -> uint64_t {
		for (;;) {
			auto check = CheckMsgData();

			fillName(check.name, sizeof(check.name), check.nameLength, "StorageManager");

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(service.ext4Port, 1, &checkMsg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(service.ext4Port, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and reply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();

		fillName(get.name, sizeof(get.name), get.nameLength, "StorageManager");

		auto getMsg = hos_msg();

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &get;
		getMsg.length = sizeof(get);

		send_horizonos_message(service.ext4Port, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(service.ext4Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.port;
	}

	auto Utils::waitForServicePort(const char *name) -> uint64_t {
		for (;;) {
			auto check = CheckMsgData();

			fillName(check.name, sizeof(check.name), check.nameLength, name);

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(service.ext4Port, 1, &checkMsg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(service.ext4Port, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and reply.exists) {
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

		send_horizonos_message(service.ext4Port, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(service.ext4Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.port;
	}

	auto Utils::registerWithVfs(const char *fsName) -> bool {
		const uint64_t vfsPort = waitForServicePort("Vfs");

		if (vfsPort == 0) {
			return false;
		}

		auto data = VfsRegisterFsHandlerMsgData();

		data.handlerPort = service.ext4Port;
		fillName(data.fsName, sizeof(data.fsName), data.fsNameLength, fsName);

		auto msg = hos_msg();

		msg.type = VFS_REGISTER_FS_HANDLER_MSG_TYPE;
		msg.port = vfsPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.ext4Port, vfsPort, &msg) != 0) {
			return false;
		}

		auto reply = VfsRegisterFsHandlerReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { VFS_REGISTER_FS_HANDLER_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.ext4Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Utils::registerFsHandler() -> bool {
		auto data = StorageRegisterFsHandlerMsgData();

		data.handlerPort = service.ext4Port;
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		fillName(data.fsName, sizeof(data.fsName), data.fsNameLength, "ext4");

		auto msg = hos_msg();

		msg.type = STORAGE_REGISTER_FS_HANDLER_MSG_TYPE;
		msg.port = service.storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(service.ext4Port, service.storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageRegisterFsHandlerReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_FS_HANDLER_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(service.ext4Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto Utils::readDevicePage(const uint64_t deviceId, const uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool {
		if (allocPhysPage(&phys) != 0) {
			return false;
		}

		if (mmap_phys(phys, 0x1000, &virt, false) != 0) {
			freePhysPage(phys);

			phys = 0;

			return false;
		}

		const ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			memset(reinterpret_cast<void *>(virt), 0, 0x1000);
			const uint64_t requestId = service.allocateStorageRequestId();
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
		printf("Ext4: Storage read failed device=%lu lba=%lu after %u attempts.", deviceId, lba, FILESYSTEM_STORAGE_ATTEMPTS);
		fflush(stdout);
		return false;
	}

	auto Utils::writeDevicePage(const uint64_t deviceId, const uint64_t lba, const uint64_t phys) -> bool {
		const ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			const uint64_t requestId = service.allocateStorageRequestId();
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

		printf("Ext4: Storage write failed device=%lu lba=%lu after %u attempts.", deviceId, lba, FILESYSTEM_STORAGE_ATTEMPTS);
		fflush(stdout);
		return false;
	}

	auto Utils::flushDevice(const uint64_t deviceId) -> bool {
		const ScopedMutex rpcLock(service.storageRpcLock);

		for (uint32_t attempt = 0; attempt < FILESYSTEM_STORAGE_ATTEMPTS; ++attempt) {
			const uint64_t requestId = service.allocateStorageRequestId();
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

		printf("Ext4: Storage flush failed device=%lu after %u attempts.", deviceId, FILESYSTEM_STORAGE_ATTEMPTS);
		fflush(stdout);
		return false;
	}

	void Utils::freeDevicePage(const uint64_t phys, const uint64_t virt) {
		if (virt != 0) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	auto Utils::inodeSize(const Ext4Superblock &superblock) -> uint16_t {
		if (superblock.revisionLevel == EXT4_GOOD_OLD_REV or superblock.inodeSize == 0) {
			return 128;
		}

		return superblock.inodeSize;
	}

	auto Utils::blocksCount(const Ext4Superblock &superblock) -> uint64_t {
		return ext4_rules::combineLowHigh(superblock.blocksCount,
		                                  superblock.blocksCountHi,
		                                  (superblock.featureIncompat & EXT4_FEATURE_INCOMPAT_64BIT) != 0);
	}

	auto Utils::inodeFileSize(const Ext4Superblock &superblock, const Ext4Inode &inode) -> uint64_t {
		if ((inode.mode & EXT4_S_IFMT) != EXT4_S_IFREG) {
			return inode.sizeLo;
		}

		if ((superblock.featureRoCompat & EXT4_FEATURE_RO_COMPAT_LARGE_FILE) == 0 and inode.sizeHighOrDirAcl == 0) {
			return inode.sizeLo;
		}

		return (static_cast<uint64_t>(inode.sizeHighOrDirAcl) << 32) | inode.sizeLo;
	}

	auto Utils::extentStartBlock(const Ext4Extent &extent) -> uint64_t {
		return (static_cast<uint64_t>(extent.startHi) << 32) | extent.startLo;
	}

	auto Utils::extentIndexLeafBlock(const Ext4ExtentIndex &index) -> uint64_t {
		return (static_cast<uint64_t>(index.leafHi) << 32) | index.leafLo;
	}

	auto Utils::extentLength(const Ext4Extent &extent) -> uint32_t {
		return extent.len & 0x7fffU;
	}

	auto Utils::groupDescriptorInodeTable(const Ext4Superblock &superblock, const Ext4GroupDescriptor &desc) -> uint64_t {
		return ext4_rules::combineLowHigh(desc.inodeTableLo,
		                                  desc.inodeTableHi,
		                                  (superblock.featureIncompat & EXT4_FEATURE_INCOMPAT_64BIT) != 0);
	}

	auto Utils::inodeNodeType(const Ext4Inode &inode) -> uint8_t {
		switch (inode.mode & EXT4_S_IFMT) {
			case EXT4_S_IFREG:
				return VFS_NODE_FILE;
			case EXT4_S_IFDIR:
				return VFS_NODE_DIRECTORY;
			case EXT4_S_IFLNK:
				return VFS_NODE_SYMLINK;
			default:
				return VFS_NODE_UNKNOWN;
		}
	}

	auto Utils::ext4DirectoryFileType(const Ext4Inode &inode) -> uint8_t {
		switch (inode.mode & EXT4_S_IFMT) {
			case EXT4_S_IFREG:
				return 1;
			case EXT4_S_IFDIR:
				return 2;
			case EXT4_S_IFLNK:
				return 7;
			default:
				return 0;
		}
	}

	auto Utils::splitPath(const string &path) -> vector<string> {
		vector<string> parts;
		size_t start = 0;

		while (start < path.size()) {
			while (start < path.size() and path[start] == '/') {
				++start;
			}

			const size_t end = path.find('/', start);
			const size_t partEnd = end == string::npos ? path.size() : end;

			if (partEnd > start) {
				parts.emplace_back(path.substr(start, partEnd - start));
			}

			if (end == string::npos) {
				break;
			}

			start = end + 1;
		}

		return parts;
	}

	auto Utils::ext4DirRecLen(const size_t nameLength) -> uint16_t {
		return static_cast<uint16_t>((8 + nameLength + 3) & ~static_cast<size_t>(3));
	}

	auto Utils::bitmapBitSet(const vector<uint8_t> &bitmap, const uint32_t bit) -> bool {
		return (bitmap[bit / 8] & (1U << (bit % 8))) != 0;
	}

	void Utils::bitmapSetBit(vector<uint8_t> &bitmap, const uint32_t bit) {
		bitmap[bit / 8] |= static_cast<uint8_t>(1U << (bit % 8));
	}

	void Utils::bitmapClearBit(vector<uint8_t> &bitmap, const uint32_t bit) {
		bitmap[bit / 8] &= static_cast<uint8_t>(~(1U << (bit % 8)));
	}

	auto Utils::probeExt4(const StorageFsProbeDeviceMsgData &device) -> bool {
		if (device.blockSize == 0 or device.blockCount == 0) {
			return false;
		}

		Ext4Volume volume(device);

		if (!volume.load()) {
			return false;
		}

		printf("Ext4: Recognized %s id=%lu blocks=%lu blockSize=%lu inodes=%u.", device.deviceName, device.deviceId, volume.getBlockCount(), volume.getBlockSize(), volume.getInodeCount());
		fflush(stdout);

		volume.testReadFirstTextFile();

		return true;
	}

	auto Utils::validPath(const char *path, const size_t length, string &out) -> bool {
		if (length == 0 or length > VFS_MAX_PATH_LENGTH or path[length - 1] != '\0') {
			return false;
		}

		out.assign(path, length - 1);

		return true;
	}
}
