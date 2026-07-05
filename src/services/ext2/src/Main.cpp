#include "StorageProtocol.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>

using namespace std;

namespace {
	constexpr uint16_t EXT2_SUPER_MAGIC = 0xEF53;
	constexpr uint64_t EXT2_SUPERBLOCK_OFFSET = 1024;

	struct Ext2Superblock {
		uint32_t inodesCount {};
		uint32_t blocksCount {};
		uint32_t reservedBlocksCount {};
		uint32_t freeBlocksCount {};
		uint32_t freeInodesCount {};
		uint32_t firstDataBlock {};
		uint32_t logBlockSize {};
		uint32_t logFragmentSize {};
		uint32_t blocksPerGroup {};
		uint32_t fragmentsPerGroup {};
		uint32_t inodesPerGroup {};
		uint32_t mtime {};
		uint32_t wtime {};
		uint16_t mountCount {};
		uint16_t maxMountCount {};
		uint16_t magic {};
	} __attribute__((packed));

	uint64_t ext2Port = 0;
	uint64_t storagePort = 0;

	void fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());
		
		memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto registerWithNameRegistry(const char *name) -> bool {
		auto msg = hos_msg();
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		fillName(data.name, sizeof(data.name), data.nameLength, name);

		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(ext2Port, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto replyMsg = hos_msg();

		replyMsg.buffer = &reply;
		replyMsg.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(ext2Port, &replyMsg, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto waitForStorage() -> uint64_t {
		for (;;) {
			auto check = CheckMsgData();

			fillName(check.name, sizeof(check.name), check.nameLength, "StorageManager");

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(ext2Port, 1, &checkMsg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(ext2Port, &recv, &filter);

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

		send_horizonos_message(ext2Port, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(ext2Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.port;
	}

	auto registerFsHandler() -> bool {
		auto data = StorageRegisterFsHandlerMsgData();

		data.handlerPort = ext2Port;
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		fillName(data.fsName, sizeof(data.fsName), data.fsNameLength, "ext2");

		auto msg = hos_msg();

		msg.type = STORAGE_REGISTER_FS_HANDLER_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(ext2Port, storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageRegisterFsHandlerReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_FS_HANDLER_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(ext2Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto readDevicePage(const uint64_t deviceId, const uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool {
		if (allocPhysPage(&phys) != 0) {
			return false;
		}

		if (mmap_phys(phys, 0x1000, &virt, false) != 0) {
			freePhysPage(phys);

			phys = 0;

			return false;
		}

		memset(reinterpret_cast<void *>(virt), 0, 0x1000);

		auto data = StorageReadMsgData();

		data.deviceId = deviceId;
		data.lba = lba;
		data.pageCount = 1;
		data.pagePhysArray[0] = phys;

		auto msg = hos_msg();

		msg.type = STORAGE_READ_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(ext2Port, storagePort, &msg) != 0) {
			munmap(reinterpret_cast<void *>(virt), 0x1000);
			freePhysPage(phys);

			phys = 0;
			virt = 0;

			return false;
		}

		auto reply = StorageReadReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_READ_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(ext2Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		if (ret != 0 or !reply.success) {
			munmap(reinterpret_cast<void *>(virt), 0x1000);
			freePhysPage(phys);

			phys = 0;
			virt = 0;

			return false;
		}

		return true;
	}

	void freeDevicePage(const uint64_t phys, const uint64_t virt) {
		if (virt != 0) {
			munmap(reinterpret_cast<void *>(virt), 0x1000);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	auto probeExt2(const StorageFsProbeDeviceMsgData &device) -> bool {
		if (device.blockSize == 0 or device.blockCount == 0) {
			return false;
		}

		const uint64_t superLba = EXT2_SUPERBLOCK_OFFSET / device.blockSize;
		const uint64_t superOffsetInPage = EXT2_SUPERBLOCK_OFFSET - (superLba * device.blockSize);

		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!readDevicePage(device.deviceId, superLba, phys, virt)) {
			return false;
		}

		const auto *superblock = reinterpret_cast<const Ext2Superblock *>(virt + superOffsetInPage);
		const bool recognized = superblock->magic == EXT2_SUPER_MAGIC;

		if (recognized) {
			const uint64_t fsBlockSize = 1024ULL << superblock->logBlockSize;

			printf("Ext2: Recognized %s id=%lu blocks=%u blockSize=%lu inodes=%u.", device.deviceName, device.deviceId, superblock->blocksCount, fsBlockSize, superblock->inodesCount);
			fflush(stdout);
		}

		freeDevicePage(phys, virt);

		return recognized;
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageFsProbeDeviceReplyMsgData();
			reply.recognized = probeExt2(data);

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_FS_PROBE_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
		}
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	if (register_horizonos_port(reinterpret_cast<long *>(&ext2Port)) != 0) {
		printf("Ext2: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!registerWithNameRegistry("Ext2")) {
		printf("Ext2: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	storagePort = waitForStorage();

	if (!registerFsHandler()) {
		printf("Ext2: Failed to register filesystem handler.");
		fflush(stdout);

		return 1;
	}

	printf("Ext2: Registered handler on port %lu with Storage port %lu.", ext2Port, storagePort);
	fflush(stdout);

	pthread_t thread;

	if (pthread_create(&thread, nullptr, probeHandler, nullptr) != 0) {
		printf("Ext2: Failed to create probe handler.");
		fflush(stdout);

		return 1;
	}

	pthread_detach(thread);

	for (;;) {
		usleep(100000);
	}
}
