#include "StorageProtocol.hpp"
#include "Ext2.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

namespace {
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
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
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
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
			freePhysPage(phys);

			phys = 0;
			virt = 0;

			return false;
		}

		return true;
	}

	auto writeDevicePage(const uint64_t deviceId, const uint64_t lba, const uint64_t phys) -> bool {
		auto data = StorageWriteMsgData();

		data.deviceId = deviceId;
		data.lba = lba;
		data.pageCount = 1;
		data.pagePhysArray[0] = phys;

		auto msg = hos_msg();

		msg.type = STORAGE_WRITE_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(ext2Port, storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageWriteReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_WRITE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(ext2Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto flushDevice(const uint64_t deviceId) -> bool {
		auto data = StorageFlushMsgData();

		data.deviceId = deviceId;

		auto msg = hos_msg();

		msg.type = STORAGE_FLUSH_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(ext2Port, storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageFlushReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_FLUSH_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(ext2Port, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	void freeDevicePage(const uint64_t phys, const uint64_t virt) {
		if (virt != 0) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	auto inodeSize(const Ext2Superblock &superblock) -> uint16_t {
		if (superblock.revisionLevel == EXT2_GOOD_OLD_REV or superblock.inodeSize == 0) {
			return 128;
		}

		return superblock.inodeSize;
	}

	auto blocksCount(const Ext2Superblock &superblock) -> uint64_t {
		return superblock.blocksCount;
	}

	auto inodeFileSize(const Ext2Superblock &superblock, const Ext2Inode &inode) -> uint64_t {
		if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
			return inode.sizeLo;
		}

		if ((superblock.featureRoCompat & EXT2_FEATURE_RO_COMPAT_LARGE_FILE) == 0 and inode.sizeHighOrDirAcl == 0) {
			return inode.sizeLo;
		}

		return (static_cast<uint64_t>(inode.sizeHighOrDirAcl) << 32) | inode.sizeLo;
	}

	auto groupDescriptorInodeTable(const Ext2Superblock &superblock, const Ext2GroupDescriptor &desc) -> uint64_t {
		(void) superblock;

		return desc.inodeTableLo;
	}

	Ext2Volume::Ext2Volume(const StorageFsProbeDeviceMsgData &device)
		: device(device) {
	}


	auto Ext2Volume::load() -> bool {
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

			memcpy(&superblock, reinterpret_cast<const void *>(virt + superOffsetInPage), sizeof(superblock));
			freeDevicePage(phys, virt);

			if (superblock.magic != EXT2_SUPER_MAGIC) {
				return false;
			}

			const uint32_t unsupportedIncompat = superblock.featureIncompat & ~EXT2_SUPPORTED_INCOMPAT_FEATURES;

			if (unsupportedIncompat != 0) {
				printf("Ext2: %s has unsupported incompatible feature flags 0x%x.", device.deviceName, unsupportedIncompat);
				fflush(stdout);

				return false;
			}

			blockSize = 1024ULL << superblock.logBlockSize;
			if (blockSize < 1024 or blockSize > 4096 or blockSize % device.blockSize != 0) {
				return false;
			}

			const uint64_t totalBlocks = blocksCount(superblock);
			const uint64_t groupCount = (totalBlocks + superblock.blocksPerGroup - 1) / superblock.blocksPerGroup;
			const uint16_t descSize = EXT2_GROUP_DESCRIPTOR_SIZE;
			const uint64_t descTableBlock = superblock.firstDataBlock + 1;
			const uint64_t descTableBytes = groupCount * descSize;
			const uint64_t descTableBlocks = (descTableBytes + blockSize - 1) / blockSize;
			vector<uint8_t> descBytes;

			descBytes.resize(descTableBlocks * blockSize);

			for (uint64_t i = 0; i < descTableBlocks; ++i) {
				if (!readBlock(descTableBlock + i, descBytes.data() + (i * blockSize))) {
					return false;
				}
			}

			groupDescriptors.clear();
			groupDescriptors.resize(groupCount);

			for (uint64_t group = 0; group < groupCount; ++group) {
				memcpy(&groupDescriptors[group], descBytes.data() + (group * descSize), min<size_t>(descSize, sizeof(Ext2GroupDescriptor)));
			}

			return true;
		}

	auto Ext2Volume::readInode(const uint32_t inodeNumber, Ext2Inode &out) -> bool {
			if (inodeNumber == 0 or inodeNumber > superblock.inodesCount) {
				return false;
			}

			const uint32_t index = inodeNumber - 1;
			const uint32_t group = index / superblock.inodesPerGroup;
			const uint32_t indexInGroup = index % superblock.inodesPerGroup;

			if (group >= groupDescriptors.size()) {
				return false;
			}

			const uint64_t tableBlock = groupDescriptorInodeTable(superblock, groupDescriptors[group]);
			const uint64_t byteOffset = static_cast<uint64_t>(indexInGroup) * inodeSize(superblock);
			const uint64_t block = tableBlock + (byteOffset / blockSize);
			const uint64_t offset = byteOffset % blockSize;
			vector<uint8_t> blockBytes;

			blockBytes.resize(blockSize);

			if (!readBlock(block, blockBytes.data())) {
				return false;
			}

			memcpy(&out, blockBytes.data() + offset, min<size_t>(sizeof(out), inodeSize(superblock)));

			return true;
		}

	auto Ext2Volume::writeInode(const uint32_t inodeNumber, const Ext2Inode &inode) -> bool {
			if (inodeNumber == 0 or inodeNumber > superblock.inodesCount) {
				return false;
			}

			const uint32_t index = inodeNumber - 1;
			const uint32_t group = index / superblock.inodesPerGroup;
			const uint32_t indexInGroup = index % superblock.inodesPerGroup;

			if (group >= groupDescriptors.size()) {
				return false;
			}

			const uint64_t tableBlock = groupDescriptorInodeTable(superblock, groupDescriptors[group]);
			const uint64_t byteOffset = static_cast<uint64_t>(indexInGroup) * inodeSize(superblock);
			const uint64_t block = tableBlock + (byteOffset / blockSize);
			const uint64_t offset = byteOffset % blockSize;
			vector<uint8_t> blockBytes;

			blockBytes.resize(blockSize);

			if (!readBlock(block, blockBytes.data())) {
				return false;
			}

			memcpy(blockBytes.data() + offset, &inode, min<size_t>(sizeof(inode), inodeSize(superblock)));

			return writeBlock(block, blockBytes.data());
		}

	auto Ext2Volume::resolveDataBlock(const Ext2Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) -> bool {
			fsBlock = 0;

			if (fileBlock < 12) {
				fsBlock = inode.block[fileBlock];

				return fsBlock != 0;
			}

			fileBlock -= 12;

			const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);

			if (fileBlock < pointersPerBlock) {
				return readIndirectPointer(inode.block[12], fileBlock, fsBlock);
			}

			fileBlock -= pointersPerBlock;

			const uint64_t doubleSpan = pointersPerBlock * pointersPerBlock;

			if (fileBlock < doubleSpan) {
				return readDoubleIndirectPointer(inode.block[13], fileBlock, fsBlock);
			}

			fileBlock -= doubleSpan;

			return readTripleIndirectPointer(inode.block[14], fileBlock, fsBlock);
		}

	auto Ext2Volume::readFile(const Ext2Inode &inode, vector<uint8_t> &out, const uint64_t maxBytes) -> bool {
			const uint64_t fileSize = inodeFileSize(superblock, inode);
			const uint64_t readSize = min<uint64_t>(fileSize, maxBytes);

			out.clear();
			out.resize(readSize);

			vector<uint8_t> blockBytes;
			blockBytes.resize(blockSize);

			uint64_t copied = 0;
			uint64_t fileBlock = 0;

			while (copied < readSize) {
				uint64_t fsBlock = 0;
				const uint64_t toCopy = min<uint64_t>(blockSize, readSize - copied);

				if (resolveDataBlock(inode, fileBlock, fsBlock)) {
					if (!readBlock(fsBlock, blockBytes.data())) {
						return false;
					}

					memcpy(out.data() + copied, blockBytes.data(), toCopy);
				} else {
					memset(out.data() + copied, 0, toCopy);
				}

				copied += toCopy;
				++fileBlock;
			}

			return true;
		}

	auto Ext2Volume::writeFileOverwrite(const uint32_t inodeNumber, Ext2Inode &inode, const uint64_t offset, const uint8_t *data, const uint64_t length) -> bool {
			(void) inodeNumber;

			const uint64_t fileSize = inodeFileSize(superblock, inode);

			if (offset > fileSize or length > fileSize - offset) {
				return false;
			}

			vector<uint8_t> blockBytes;
			blockBytes.resize(blockSize);

			uint64_t written = 0;

			while (written < length) {
				const uint64_t fileOffset = offset + written;
				const uint64_t fileBlock = fileOffset / blockSize;
				const uint64_t blockOffset = fileOffset % blockSize;
				const uint64_t toCopy = min<uint64_t>(blockSize - blockOffset, length - written);
				uint64_t fsBlock = 0;

				if (!resolveDataBlock(inode, fileBlock, fsBlock)) {
					return false;
				}

				if (!readBlock(fsBlock, blockBytes.data())) {
					return false;
				}

				memcpy(blockBytes.data() + blockOffset, data + written, toCopy);

				if (!writeBlock(fsBlock, blockBytes.data())) {
					return false;
				}

				written += toCopy;
			}

			return flushDevice(device.deviceId);
		}

	auto Ext2Volume::findFirstRootTextFile(uint32_t &inodeNumber, string &name) -> bool {
			Ext2Inode root {};

			if (!readInode(EXT2_ROOT_INO, root) or (root.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
				return false;
			}

			vector<uint8_t> dirBytes;

			if (!readFile(root, dirBytes, inodeFileSize(superblock, root))) {
				return false;
			}

			uint64_t offset = 0;

			while (offset + 8 <= dirBytes.size()) {
				const auto *entry = reinterpret_cast<const Ext2DirEntry *>(dirBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
					break;
				}

				if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8) {
					string candidate(entry->name, entry->nameLen);

					if (candidate != "." and candidate != ".." and candidate.ends_with(".txt")) {
						Ext2Inode candidateInode {};

						if (readInode(entry->inode, candidateInode) and (candidateInode.mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
							inodeNumber = entry->inode;
							name = candidate;

							return true;
						}
					}
				}

				offset += entry->recLen;
			}

			return false;
		}

	void Ext2Volume::testReadFirstTextFile() {
			uint32_t inodeNumber = 0;
			string name;

			if (!findFirstRootTextFile(inodeNumber, name)) {
				printf("Ext2: %s has no root .txt file to test.", device.deviceName);
				fflush(stdout);

				return;
			}

			Ext2Inode inode {};
			vector<uint8_t> bytes;

			if (!readInode(inodeNumber, inode) or !readFile(inode, bytes, 4096)) {
				printf("Ext2: Failed to read %s/%s.", device.deviceName, name.c_str());
				fflush(stdout);

				return;
			}

			string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());

			printf("Ext2: First text file on %s is %s (%lu bytes): %s", device.deviceName, name.c_str(), inodeFileSize(superblock, inode), text.c_str());
			fflush(stdout);

			if (!bytes.empty()) {
				const uint8_t firstByte = bytes[0];
				vector<uint8_t> reread;

				if (writeFileOverwrite(inodeNumber, inode, 0, &firstByte, 1) and readInode(inodeNumber, inode) and readFile(inode, reread, bytes.size()) and reread == bytes) {
					printf("Ext2: Verified overwrite write path on %s/%s.", device.deviceName, name.c_str());
					fflush(stdout);
				} else {
					printf("Ext2: Overwrite write path verification failed on %s/%s.", device.deviceName, name.c_str());
					fflush(stdout);
				}
			}
		}

	auto Ext2Volume::getBlockSize() const -> uint64_t {
			return blockSize;
		}

	auto Ext2Volume::getInodeCount() const -> uint32_t {
			return superblock.inodesCount;
		}

	auto Ext2Volume::getBlockCount() const -> uint64_t {
			return blocksCount(superblock);
		}
	auto Ext2Volume::readBlock(const uint64_t fsBlock, uint8_t *buffer) -> bool {
			if (blockSize == 0) {
				return false;
			}

			const uint64_t byteOffset = fsBlock * blockSize;
			const uint64_t lba = byteOffset / device.blockSize;
			uint64_t phys = 0;
			uint64_t virt = 0;

			if (!readDevicePage(device.deviceId, lba, phys, virt)) {
				return false;
			}

			memcpy(buffer, reinterpret_cast<const void *>(virt), blockSize);
			freeDevicePage(phys, virt);

			return true;
		}

	auto Ext2Volume::writeBlock(const uint64_t fsBlock, const uint8_t *buffer) -> bool {
			if (blockSize == 0) {
				return false;
			}

			const uint64_t byteOffset = fsBlock * blockSize;
			const uint64_t lba = byteOffset / device.blockSize;
			uint64_t phys = 0;
			uint64_t virt = 0;

			if (!readDevicePage(device.deviceId, lba, phys, virt)) {
				return false;
			}

			memcpy(reinterpret_cast<void *>(virt), buffer, blockSize);

			const bool success = writeDevicePage(device.deviceId, lba, phys);

			freeDevicePage(phys, virt);

			return success;
		}

	auto Ext2Volume::readIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) -> bool {
			if (block == 0) {
				return false;
			}

			vector<uint8_t> bytes;
			bytes.resize(blockSize);

			if (!readBlock(block, bytes.data())) {
				return false;
			}

			const auto *pointers = reinterpret_cast<const uint32_t *>(bytes.data());

			fsBlock = pointers[index];

			return fsBlock != 0;
		}

	auto Ext2Volume::readDoubleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) -> bool {
			if (block == 0) {
				return false;
			}

			const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);
			uint64_t indirectBlock = 0;

			if (!readIndirectPointer(block, index / pointersPerBlock, indirectBlock)) {
				return false;
			}

			return readIndirectPointer(static_cast<uint32_t>(indirectBlock), index % pointersPerBlock, fsBlock);
		}

	auto Ext2Volume::readTripleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) -> bool {
			if (block == 0) {
				return false;
			}

			const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);
			const uint64_t doubleSpan = pointersPerBlock * pointersPerBlock;
			uint64_t doubleBlock = 0;

			if (!readIndirectPointer(block, index / doubleSpan, doubleBlock)) {
				return false;
			}

			return readDoubleIndirectPointer(static_cast<uint32_t>(doubleBlock), index % doubleSpan, fsBlock);
		}


	auto probeExt2(const StorageFsProbeDeviceMsgData &device) -> bool {
		if (device.blockSize == 0 or device.blockCount == 0) {
			return false;
		}

		Ext2Volume volume(device);

		if (!volume.load()) {
			return false;
		}

		printf("Ext2: Recognized %s id=%lu blocks=%lu blockSize=%lu inodes=%u.", device.deviceName, device.deviceId, volume.getBlockCount(), volume.getBlockSize(), volume.getInodeCount());
		fflush(stdout);

		volume.testReadFirstTextFile();

		return true;
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
