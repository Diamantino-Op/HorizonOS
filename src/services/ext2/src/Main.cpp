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
	uint64_t nextMountId = 1;

	struct MountedExt2 {
		uint64_t mountId {};
		StorageFsProbeDeviceMsgData device {};
	};

	vector<MountedExt2> mounts;

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

	auto inodeNodeType(const Ext2Inode &inode) -> uint8_t {
		switch (inode.mode & EXT2_S_IFMT) {
			case EXT2_S_IFREG:
				return VFS_NODE_FILE;
			case EXT2_S_IFDIR:
				return VFS_NODE_DIRECTORY;
			case EXT2_S_IFLNK:
				return VFS_NODE_SYMLINK;
			default:
				return VFS_NODE_UNKNOWN;
		}
	}

	auto ext2DirectoryFileType(const Ext2Inode &inode) -> uint8_t {
		switch (inode.mode & EXT2_S_IFMT) {
			case EXT2_S_IFREG:
				return 1;
			case EXT2_S_IFDIR:
				return 2;
			case EXT2_S_IFLNK:
				return 7;
			default:
				return 0;
		}
	}

	auto splitPath(const string &path) -> vector<string> {
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

	auto ext2DirRecLen(const size_t nameLength) -> uint16_t {
		return static_cast<uint16_t>((8 + nameLength + 3) & ~static_cast<size_t>(3));
	}

	auto bitmapBitSet(const vector<uint8_t> &bitmap, const uint32_t bit) -> bool {
		return (bitmap[bit / 8] & (1U << (bit % 8))) != 0;
	}

	void bitmapSetBit(vector<uint8_t> &bitmap, const uint32_t bit) {
		bitmap[bit / 8] |= static_cast<uint8_t>(1U << (bit % 8));
	}

	void bitmapClearBit(vector<uint8_t> &bitmap, const uint32_t bit) {
		bitmap[bit / 8] &= static_cast<uint8_t>(~(1U << (bit % 8)));
	}

	Ext2Volume::Ext2Volume(const StorageFsProbeDeviceMsgData &device)
		: device(device) {
	}


	auto Ext2Volume::load() -> bool {
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

	auto Ext2Volume::readInode(const uint32_t inodeNumber, Ext2Inode &out) const -> bool {
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

	auto Ext2Volume::writeInode(const uint32_t inodeNumber, const Ext2Inode &inode) const -> bool {
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

	auto Ext2Volume::resolveDataBlock(const Ext2Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) const -> bool {
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

	auto Ext2Volume::readFile(const Ext2Inode &inode, vector<uint8_t> &out, const uint64_t maxBytes) const -> bool {
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

	auto Ext2Volume::readFileRange(const Ext2Inode &inode, const uint64_t offset, const uint32_t length, vector<uint8_t> &out) const -> bool {
		const uint64_t fileSize = inodeFileSize(superblock, inode);

		out.clear();

		if (offset >= fileSize or length == 0) {
			return true;
		}

		const uint64_t readSize = min<uint64_t>(length, fileSize - offset);

		out.resize(readSize);

		vector<uint8_t> blockBytes;
		blockBytes.resize(blockSize);

		uint64_t copied = 0;

		while (copied < readSize) {
			const uint64_t fileOffset = offset + copied;
			const uint64_t fileBlock = fileOffset / blockSize;
			const uint64_t blockOffset = fileOffset % blockSize;
			const uint64_t toCopy = min<uint64_t>(blockSize - blockOffset, readSize - copied);
			uint64_t fsBlock = 0;

			if (resolveDataBlock(inode, fileBlock, fsBlock)) {
				if (!readBlock(fsBlock, blockBytes.data())) {
					return false;
				}

				memcpy(out.data() + copied, blockBytes.data() + blockOffset, toCopy);
			} else {
				memset(out.data() + copied, 0, toCopy);
			}

			copied += toCopy;
		}

		return true;
	}

	auto Ext2Volume::writeFileOverwrite(const uint32_t inodeNumber, Ext2Inode &inode, const uint64_t offset, const uint8_t *data, const uint64_t length) const -> bool {
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

	void Ext2Volume::setInodeFileSize(Ext2Inode &inode, const uint64_t size) {
		inode.sizeLo = static_cast<uint32_t>(size);

		if ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
			inode.sizeHighOrDirAcl = static_cast<uint32_t>(size >> 32);
		}
	}

	auto Ext2Volume::writeSuperblock() const -> bool {
		const uint64_t superLba = EXT2_SUPERBLOCK_OFFSET / device.blockSize;
		const uint64_t superOffsetInPage = EXT2_SUPERBLOCK_OFFSET - (superLba * device.blockSize);
		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!readDevicePage(device.deviceId, superLba, phys, virt)) {
			return false;
		}

		memcpy(reinterpret_cast<void *>(virt + superOffsetInPage), &superblock, sizeof(superblock));

		const bool success = writeDevicePage(device.deviceId, superLba, phys);

		freeDevicePage(phys, virt);

		return success;
	}

	auto Ext2Volume::writeGroupDescriptor(const uint32_t group) const -> bool {
		if (group >= groupDescriptors.size()) {
			return false;
		}

		const uint64_t descTableBlock = superblock.firstDataBlock + 1;
		const uint64_t offset = static_cast<uint64_t>(group) * EXT2_GROUP_DESCRIPTOR_SIZE;
		const uint64_t block = descTableBlock + (offset / blockSize);
		const uint64_t blockOffset = offset % blockSize;
		vector<uint8_t> bytes;

		bytes.resize(blockSize);

		if (!readBlock(block, bytes.data())) {
			return false;
		}

		memcpy(bytes.data() + blockOffset, &groupDescriptors[group], EXT2_GROUP_DESCRIPTOR_SIZE);

		return writeBlock(block, bytes.data());
	}

	auto Ext2Volume::allocateBlock(uint32_t &block) -> bool {
		block = 0;

		for (uint32_t group = 0; group < groupDescriptors.size(); ++group) {
			if (groupDescriptors[group].freeBlocksCountLo == 0) {
				continue;
			}

			vector<uint8_t> bitmap;

			bitmap.resize(blockSize);

			if (!readBlock(groupDescriptors[group].blockBitmapLo, bitmap.data())) {
				return false;
			}

			for (uint32_t bit = 0; bit < superblock.blocksPerGroup; ++bit) {
				const uint64_t candidate = static_cast<uint64_t>(superblock.firstDataBlock) + (static_cast<uint64_t>(group) * superblock.blocksPerGroup) + bit;

				if (candidate == 0 or candidate >= blocksCount(superblock)) {
					break;
				}

				if (bitmapBitSet(bitmap, bit)) {
					continue;
				}

				bitmapSetBit(bitmap, bit);
				block = static_cast<uint32_t>(candidate);

				--superblock.freeBlocksCount;
				--groupDescriptors[group].freeBlocksCountLo;

				vector<uint8_t> zeros;

				zeros.resize(blockSize);

				return writeBlock(groupDescriptors[group].blockBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock() and writeBlock(block, zeros.data());
			}
		}

		return false;
	}

	auto Ext2Volume::allocateInode(uint32_t &inodeNumber) -> bool {
		inodeNumber = 0;

		for (uint32_t group = 0; group < groupDescriptors.size(); ++group) {
			if (groupDescriptors[group].freeInodesCountLo == 0) {
				continue;
			}

			vector<uint8_t> bitmap;

			bitmap.resize(blockSize);

			if (!readBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data())) {
				return false;
			}

			for (uint32_t bit = 0; bit < superblock.inodesPerGroup; ++bit) {
				const uint64_t candidate = (static_cast<uint64_t>(group) * superblock.inodesPerGroup) + bit + 1;

				if (candidate < superblock.firstInode or candidate > superblock.inodesCount) {
					continue;
				}

				if (bitmapBitSet(bitmap, bit)) {
					continue;
				}

				bitmapSetBit(bitmap, bit);
				inodeNumber = static_cast<uint32_t>(candidate);

				--superblock.freeInodesCount;
				--groupDescriptors[group].freeInodesCountLo;

				return writeBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
			}
		}

		return false;
	}

	auto Ext2Volume::freeBlock(const uint32_t block) -> bool {
		if (block < superblock.firstDataBlock or block >= blocksCount(superblock)) {
			return false;
		}

		const uint32_t group = (block - superblock.firstDataBlock) / superblock.blocksPerGroup;
		const uint32_t bit = (block - superblock.firstDataBlock) % superblock.blocksPerGroup;

		if (group >= groupDescriptors.size()) {
			return false;
		}

		vector<uint8_t> bitmap;

		bitmap.resize(blockSize);

		if (!readBlock(groupDescriptors[group].blockBitmapLo, bitmap.data())) {
			return false;
		}

		if (!bitmapBitSet(bitmap, bit)) {
			return true;
		}

		bitmapClearBit(bitmap, bit);
		++superblock.freeBlocksCount;
		++groupDescriptors[group].freeBlocksCountLo;

		return writeBlock(groupDescriptors[group].blockBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
	}

	auto Ext2Volume::freeInode(const uint32_t inodeNumber) -> bool {
		if (inodeNumber < superblock.firstInode or inodeNumber > superblock.inodesCount) {
			return false;
		}

		const uint32_t index = inodeNumber - 1;
		const uint32_t group = index / superblock.inodesPerGroup;
		const uint32_t bit = index % superblock.inodesPerGroup;

		if (group >= groupDescriptors.size()) {
			return false;
		}

		vector<uint8_t> bitmap;

		bitmap.resize(blockSize);

		if (!readBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data())) {
			return false;
		}

		if (!bitmapBitSet(bitmap, bit)) {
			return true;
		}

		bitmapClearBit(bitmap, bit);
		++superblock.freeInodesCount;
		++groupDescriptors[group].freeInodesCountLo;

		return writeBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
	}

	auto Ext2Volume::ensureIndirectDataBlock(Ext2Inode &inode, uint32_t &pointerBlock, const uint32_t depth, const uint64_t index, uint32_t &fsBlock) -> bool {
		const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);
		uint64_t span = 1;

		for (uint32_t i = 1; i < depth; ++i) {
			span *= pointersPerBlock;
		}

		if (index >= span * pointersPerBlock) {
			return false;
		}

		if (pointerBlock == 0) {
			if (!allocateBlock(pointerBlock)) {
				return false;
			}

			inode.blocks += static_cast<uint32_t>(blockSize / 512);
		}

		vector<uint8_t> bytes;

		bytes.resize(blockSize);

		if (!readBlock(pointerBlock, bytes.data())) {
			return false;
		}

		auto *pointers = reinterpret_cast<uint32_t *>(bytes.data());
		const uint64_t slot = depth == 1 ? index : index / span;

		if (slot >= pointersPerBlock) {
			return false;
		}

		if (depth == 1) {
			if (pointers[slot] == 0) {
				if (!allocateBlock(fsBlock)) {
					return false;
				}

				pointers[slot] = fsBlock;
				inode.blocks += static_cast<uint32_t>(blockSize / 512);

				return writeBlock(pointerBlock, bytes.data());
			}

			fsBlock = pointers[slot];

			return true;
		}

		uint32_t childPointerBlock = pointers[slot];

		if (!ensureIndirectDataBlock(inode, childPointerBlock, depth - 1, index % span, fsBlock)) {
			return false;
		}

		if (pointers[slot] != childPointerBlock) {
			pointers[slot] = childPointerBlock;

			return writeBlock(pointerBlock, bytes.data());
		}

		return true;
	}

	auto Ext2Volume::ensureDataBlock(Ext2Inode &inode, uint64_t fileBlock, uint32_t &fsBlock) -> bool {
		fsBlock = 0;

		uint64_t existing = 0;

		if (resolveDataBlock(inode, fileBlock, existing)) {
			fsBlock = static_cast<uint32_t>(existing);

			return true;
		}

		if (fileBlock < 12) {
			if (!allocateBlock(fsBlock)) {
				return false;
			}

			inode.block[fileBlock] = fsBlock;
			inode.blocks += static_cast<uint32_t>(blockSize / 512);

			return true;
		}

		fileBlock -= 12;

		const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);

		if (fileBlock < pointersPerBlock) {
			return ensureIndirectDataBlock(inode, inode.block[12], 1, fileBlock, fsBlock);
		}

		fileBlock -= pointersPerBlock;

		const uint64_t doubleSpan = pointersPerBlock * pointersPerBlock;

		if (fileBlock < doubleSpan) {
			return ensureIndirectDataBlock(inode, inode.block[13], 2, fileBlock, fsBlock);
		}

		fileBlock -= doubleSpan;

		return ensureIndirectDataBlock(inode, inode.block[14], 3, fileBlock, fsBlock);
	}

	auto Ext2Volume::writeFile(const uint32_t inodeNumber, Ext2Inode &inode, const uint64_t offset, const uint8_t *data, const uint64_t length) -> bool {
		if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG or offset > (1ULL << 47)) {
			return false;
		}

		if (length == 0) {
			return true;
		}

		const uint64_t newSize = max<uint64_t>(inodeFileSize(superblock, inode), offset + length);
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		uint64_t written = 0;

		while (written < length) {
			const uint64_t fileOffset = offset + written;
			const uint64_t fileBlock = fileOffset / blockSize;
			const uint64_t blockOffset = fileOffset % blockSize;
			const uint64_t toCopy = min<uint64_t>(blockSize - blockOffset, length - written);
			uint32_t fsBlock = 0;

			if (!ensureDataBlock(inode, fileBlock, fsBlock)) {
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

		if (newSize > 0xffffffffULL and (superblock.featureRoCompat & EXT2_FEATURE_RO_COMPAT_LARGE_FILE) == 0) {
			superblock.featureRoCompat |= EXT2_FEATURE_RO_COMPAT_LARGE_FILE;

			if (!writeSuperblock()) {
				return false;
			}
		}

		setInodeFileSize(inode, newSize);

		return writeInode(inodeNumber, inode) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::splitParentPath(const string &path, string &parentPath, string &name) -> bool {
		if (path.empty() or path == "/") {
			return false;
		}

		const size_t end = path.find_last_not_of('/');

		if (end == string::npos) {
			return false;
		}

		const size_t slash = path.find_last_of('/', end);

		name = path.substr(slash == string::npos ? 0 : slash + 1, end - (slash == string::npos ? 0 : slash + 1) + 1);
		parentPath = slash == string::npos or slash == 0 ? "/" : path.substr(0, slash);

		return !name.empty() and name.size() < VFS_MAX_NAME_LENGTH;
	}

	auto Ext2Volume::addDirectoryEntry(const uint32_t parentInodeNumber, Ext2Inode &parent, const uint32_t childInodeNumber, const string &name, const uint8_t fileType) -> bool {
		if ((parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR or name.empty() or name.size() > 255) {
			return false;
		}

		const uint16_t needed = ext2DirRecLen(name.size());
		const uint64_t parentSize = inodeFileSize(superblock, parent);
		const uint64_t blocks = max<uint64_t>(1, (parentSize + blockSize - 1) / blockSize);
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		for (uint64_t fileBlock = 0; fileBlock < blocks; ++fileBlock) {
			uint32_t fsBlock = 0;

			if (!ensureDataBlock(parent, fileBlock, fsBlock)) {
				return false;
			}

			if (!readBlock(fsBlock, blockBytes.data())) {
				return false;
			}

			uint64_t offset = 0;

			while (offset + 8 <= blockSize) {
				auto *entry = reinterpret_cast<Ext2DirEntry *>(blockBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > blockSize) {
					break;
				}

				const uint16_t actual = entry->inode == 0 ? 8 : ext2DirRecLen(entry->nameLen);
				const uint16_t available = entry->recLen;

				if (available >= actual + needed) {
					entry->recLen = actual;

					auto *newEntry = reinterpret_cast<Ext2DirEntry *>(blockBytes.data() + offset + actual);

					newEntry->inode = childInodeNumber;
					newEntry->recLen = available - actual;
					newEntry->nameLen = static_cast<uint8_t>(name.size());
					newEntry->fileType = fileType;
					memcpy(newEntry->name, name.data(), name.size());

					return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and flushDevice(device.deviceId);
				}

				offset += entry->recLen;
			}
		}

		const uint64_t newFileBlock = blocks;
		uint32_t fsBlock = 0;

		if (!ensureDataBlock(parent, newFileBlock, fsBlock)) {
			return false;
		}

		memset(blockBytes.data(), 0, blockSize);

		auto *entry = reinterpret_cast<Ext2DirEntry *>(blockBytes.data());

		entry->inode = childInodeNumber;
		entry->recLen = static_cast<uint16_t>(blockSize);
		entry->nameLen = static_cast<uint8_t>(name.size());
		entry->fileType = fileType;
		memcpy(entry->name, name.data(), name.size());

		setInodeFileSize(parent, parentSize + blockSize);

		return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::removeDirectoryEntry(const uint32_t parentInodeNumber, Ext2Inode &parent, const string &name, uint32_t &removedInode) -> bool {
		removedInode = 0;

		if ((parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR or name.empty()) {
			return false;
		}

		const uint64_t parentSize = inodeFileSize(superblock, parent);
		const uint64_t blocks = (parentSize + blockSize - 1) / blockSize;
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		for (uint64_t fileBlock = 0; fileBlock < blocks; ++fileBlock) {
			uint32_t fsBlock = 0;

			if (!ensureDataBlock(parent, fileBlock, fsBlock) or !readBlock(fsBlock, blockBytes.data())) {
				return false;
			}

			uint64_t offset = 0;
			Ext2DirEntry *previous = nullptr;

			while (offset + 8 <= blockSize) {
				auto *entry = reinterpret_cast<Ext2DirEntry *>(blockBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > blockSize) {
					break;
				}

				if (entry->inode != 0 and entry->nameLen == name.size() and memcmp(entry->name, name.data(), name.size()) == 0) {
					removedInode = entry->inode;

					if (previous != nullptr) {
						previous->recLen += entry->recLen;
					} else {
						entry->inode = 0;
					}

					return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and flushDevice(device.deviceId);
				}

				if (entry->inode != 0) {
					previous = entry;
				}

				offset += entry->recLen;
			}
		}

		return false;
	}

	auto Ext2Volume::directoryIsEmpty(const Ext2Inode &dir) const -> bool {
		if ((dir.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		vector<uint8_t> dirBytes;

		if (!readFile(dir, dirBytes, inodeFileSize(superblock, dir))) {
			return false;
		}

		uint64_t offset = 0;

		while (offset + 8 <= dirBytes.size()) {
			const auto *entry = reinterpret_cast<const Ext2DirEntry *>(dirBytes.data() + offset);

			if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
				return false;
			}

			if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8) {
				const string name(entry->name, entry->nameLen);

				if (name != "." and name != "..") {
					return false;
				}
			}

			offset += entry->recLen;
		}

		return true;
	}

	auto Ext2Volume::updateDirectoryEntryInode(const Ext2Inode &dir, const string &name, const uint32_t inodeNumber) const -> bool {
		if ((dir.mode & EXT2_S_IFMT) != EXT2_S_IFDIR or name.empty()) {
			return false;
		}

		const uint64_t dirSize = inodeFileSize(superblock, dir);
		const uint64_t blocks = (dirSize + blockSize - 1) / blockSize;
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		for (uint64_t fileBlock = 0; fileBlock < blocks; ++fileBlock) {
			uint64_t fsBlock = 0;

			if (!resolveDataBlock(dir, fileBlock, fsBlock) or !readBlock(fsBlock, blockBytes.data())) {
				return false;
			}

			uint64_t offset = 0;

			while (offset + 8 <= blockSize) {
				auto *entry = reinterpret_cast<Ext2DirEntry *>(blockBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > blockSize) {
					break;
				}

				if (entry->inode != 0 and entry->nameLen == name.size() and memcmp(entry->name, name.data(), name.size()) == 0) {
					entry->inode = inodeNumber;

					return writeBlock(fsBlock, blockBytes.data());
				}

				offset += entry->recLen;
			}
		}

		return false;
	}

	auto Ext2Volume::freeIndirectBlocks(uint32_t &pointerBlock, const uint32_t depth, const uint64_t keepBlocks, const uint64_t span) -> bool {
		if (pointerBlock == 0) {
			return true;
		}

		vector<uint8_t> bytes;

		bytes.resize(blockSize);

		if (!readBlock(pointerBlock, bytes.data())) {
			return false;
		}

		auto *pointers = reinterpret_cast<uint32_t *>(bytes.data());
		const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);
		bool anyLive = false;

		for (uint64_t slot = 0; slot < pointersPerBlock; ++slot) {
			if (pointers[slot] == 0) {
				continue;
			}

			const uint64_t slotStart = slot * span;
			const uint64_t slotEnd = slotStart + span;

			if (keepBlocks >= slotEnd) {
				anyLive = true;

				continue;
			}

			if (depth == 1) {
				if (!freeBlock(pointers[slot])) {
					return false;
				}

				pointers[slot] = 0;
			} else if (keepBlocks <= slotStart) {
				uint32_t child = pointers[slot];

				if (!freeIndirectBlocks(child, depth - 1, 0, span / pointersPerBlock)) {
					return false;
				}

				pointers[slot] = 0;
			} else {
				uint32_t child = pointers[slot];

				if (!freeIndirectBlocks(child, depth - 1, keepBlocks - slotStart, span / pointersPerBlock)) {
					return false;
				}

				pointers[slot] = child;
			}

			if (pointers[slot] != 0) {
				anyLive = true;
			}
		}

		if (!anyLive) {
			if (!freeBlock(pointerBlock)) {
				return false;
			}

			pointerBlock = 0;

			return true;
		}

		return writeBlock(pointerBlock, bytes.data());
	}

	auto Ext2Volume::freeInodeBlocks(Ext2Inode &inode, const uint64_t keepBlocks) -> bool {
		const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);

		for (uint64_t i = 0; i < 12; ++i) {
			if (i >= keepBlocks and inode.block[i] != 0) {
				if (!freeBlock(inode.block[i])) {
					return false;
				}

				inode.block[i] = 0;
			}
		}

		uint64_t remaining = keepBlocks > 12 ? keepBlocks - 12 : 0;

		if (!freeIndirectBlocks(inode.block[12], 1, min<uint64_t>(remaining, pointersPerBlock), 1)) {
			return false;
		}

		remaining = remaining > pointersPerBlock ? remaining - pointersPerBlock : 0;

		if (!freeIndirectBlocks(inode.block[13], 2, min<uint64_t>(remaining, pointersPerBlock * pointersPerBlock), pointersPerBlock)) {
			return false;
		}

		remaining = remaining > pointersPerBlock * pointersPerBlock ? remaining - (pointersPerBlock * pointersPerBlock) : 0;

		return freeIndirectBlocks(inode.block[14], 3, remaining, pointersPerBlock * pointersPerBlock);
	}

	auto Ext2Volume::countIndirectBlocks(const uint32_t pointerBlock, const uint32_t depth) const -> uint64_t {
		if (pointerBlock == 0) {
			return 0;
		}

		uint64_t count = 1;
		vector<uint8_t> bytes;

		bytes.resize(blockSize);

		if (!readBlock(pointerBlock, bytes.data())) {
			return count;
		}

		const auto *pointers = reinterpret_cast<const uint32_t *>(bytes.data());
		const uint64_t pointersPerBlock = blockSize / sizeof(uint32_t);

		for (uint64_t slot = 0; slot < pointersPerBlock; ++slot) {
			if (pointers[slot] == 0) {
				continue;
			}

			count += depth == 1 ? 1 : countIndirectBlocks(pointers[slot], depth - 1);
		}

		return count;
	}

	auto Ext2Volume::countInodeBlocks(const Ext2Inode &inode) const -> uint64_t {
		uint64_t count = 0;

		for (uint32_t i = 0; i < 12; ++i) {
			if (inode.block[i] != 0) {
				++count;
			}
		}

		count += countIndirectBlocks(inode.block[12], 1);
		count += countIndirectBlocks(inode.block[13], 2);
		count += countIndirectBlocks(inode.block[14], 3);

		return count;
	}

	auto Ext2Volume::createFile(const string &path) -> bool {
		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name)) {
			return false;
		}

		uint32_t existingInode = 0;
		Ext2Inode existing {};

		if (lookupPath(path, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext2Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		auto inode = Ext2Inode();

		inode.mode = EXT2_S_IFREG | 0644;
		inode.linksCount = 1;

		if (!writeInode(inodeNumber, inode)) {
			return false;
		}

		return addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, 1);
	}

	auto Ext2Volume::createDirectory(const string &path) -> bool {
		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t existingInode = 0;
		Ext2Inode existing {};

		if (lookupPath(path, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext2Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		uint32_t dirBlock = 0;

		if (!allocateBlock(dirBlock)) {
			return false;
		}

		auto inode = Ext2Inode();

		inode.mode = EXT2_S_IFDIR | 0755;
		inode.linksCount = 2;
		inode.sizeLo = static_cast<uint32_t>(blockSize);
		inode.blocks = static_cast<uint32_t>(blockSize / 512);
		inode.block[0] = dirBlock;

		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		auto *dot = reinterpret_cast<Ext2DirEntry *>(blockBytes.data());

		dot->inode = inodeNumber;
		dot->recLen = ext2DirRecLen(1);
		dot->nameLen = 1;
		dot->fileType = 2;
		dot->name[0] = '.';

		auto *dotDot = reinterpret_cast<Ext2DirEntry *>(blockBytes.data() + dot->recLen);

		dotDot->inode = parentInodeNumber;
		dotDot->recLen = static_cast<uint16_t>(blockSize - dot->recLen);
		dotDot->nameLen = 2;
		dotDot->fileType = 2;
		dotDot->name[0] = '.';
		dotDot->name[1] = '.';

		if (!writeBlock(dirBlock, blockBytes.data()) or !writeInode(inodeNumber, inode)) {
			return false;
		}

		++parent.linksCount;

		if (!addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, 2)) {
			return false;
		}

		const uint32_t group = (inodeNumber - 1) / superblock.inodesPerGroup;

		if (group < groupDescriptors.size()) {
			++groupDescriptors[group].usedDirsCountLo;

			if (!writeGroupDescriptor(group)) {
				return false;
			}
		}

		return writeInode(parentInodeNumber, parent) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::createHardLink(const string &oldPath, const string &newPath) -> bool {
		string parentPath;
		string name;

		if (!splitParentPath(newPath, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t existingInode = 0;
		Ext2Inode existing {};

		if (lookupPath(newPath, existingInode, existing)) {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext2Inode inode {};

		if (!lookupPath(oldPath, inodeNumber, inode) or (inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext2Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		if (!addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, ext2DirectoryFileType(inode))) {
			return false;
		}

		++inode.linksCount;

		return writeInode(inodeNumber, inode) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::createSymlink(const string &target, const string &linkPath) -> bool {
		string parentPath;
		string name;

		if (target.empty() or target.size() >= VFS_MAX_PATH_LENGTH or !splitParentPath(linkPath, parentPath, name)) {
			return false;
		}

		uint32_t existingInode = 0;
		Ext2Inode existing {};

		if (lookupPath(linkPath, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext2Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		auto inode = Ext2Inode();

		inode.mode = EXT2_S_IFLNK | 0777;
		inode.linksCount = 1;
		inode.sizeLo = target.size();

		if (target.size() <= sizeof(inode.block)) {
			memcpy(inode.block, target.data(), target.size());
		} else {
			uint32_t block = 0;

			if (!allocateBlock(block)) {
				return false;
			}

			vector<uint8_t> bytes;

			bytes.resize(blockSize);
			memcpy(bytes.data(), target.data(), target.size());
			inode.block[0] = block;
			inode.blocks = static_cast<uint32_t>(blockSize / 512);

			if (!writeBlock(block, bytes.data())) {
				return false;
			}
		}

		if (!writeInode(inodeNumber, inode)) {
			return false;
		}

		return addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, 7);
	}

	auto Ext2Volume::readSymlink(const string &linkPath, string &target) -> bool {
		Ext2Inode inode {};
		uint32_t inodeNumber = 0;

		if (!lookupPath(linkPath, inodeNumber, inode) or inodeNodeType(inode) != VFS_NODE_SYMLINK) {
			return false;
		}

		(void) inodeNumber;

		const uint64_t size = inodeFileSize(superblock, inode);

		if (size >= VFS_MAX_PATH_LENGTH) {
			return false;
		}

		if (size <= sizeof(inode.block) and inode.blocks == 0) {
			target.assign(reinterpret_cast<const char *>(inode.block), size);
			return true;
		}

		vector<uint8_t> bytes;

		if (!readFileRange(inode, 0, static_cast<uint32_t>(size), bytes)) {
			return false;
		}

		target.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());

		return true;
	}

	auto Ext2Volume::truncateFile(const string &path, const uint64_t size) -> bool {
		uint32_t inodeNumber = 0;
		Ext2Inode inode {};

		if (!lookupPath(path, inodeNumber, inode) or (inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
			return false;
		}

		const uint64_t keepBlocks = (size + blockSize - 1) / blockSize;

		if (size > 0) {
			for (uint64_t fileBlock = 0; fileBlock < keepBlocks; ++fileBlock) {
				uint32_t fsBlock = 0;

				if (!ensureDataBlock(inode, fileBlock, fsBlock)) {
					return false;
				}
			}
		}

		if (!freeInodeBlocks(inode, keepBlocks)) {
			return false;
		}

		if (size > 0xffffffffULL and (superblock.featureRoCompat & EXT2_FEATURE_RO_COMPAT_LARGE_FILE) == 0) {
			superblock.featureRoCompat |= EXT2_FEATURE_RO_COMPAT_LARGE_FILE;

			if (!writeSuperblock()) {
				return false;
			}
		}

		setInodeFileSize(inode, size);
		inode.blocks = static_cast<uint32_t>(countInodeBlocks(inode) * (blockSize / 512));

		return writeInode(inodeNumber, inode) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::unlinkFile(const string &path) -> bool {
		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext2Inode inode {};

		if (!lookupPath(path, inodeNumber, inode) or inodeNumber == EXT2_ROOT_INO) {
			return false;
		}

		const uint16_t nodeMode = inode.mode & EXT2_S_IFMT;

		if (nodeMode != EXT2_S_IFREG and nodeMode != EXT2_S_IFDIR) {
			return false;
		}

		if (nodeMode == EXT2_S_IFDIR and !directoryIsEmpty(inode)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext2Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		uint32_t removedInode = 0;

		if (!removeDirectoryEntry(parentInodeNumber, parent, name, removedInode) or removedInode != inodeNumber) {
			return false;
		}

		if (!freeInodeBlocks(inode, 0)) {
			return false;
		}

		if (nodeMode == EXT2_S_IFDIR) {
			if (parent.linksCount > 0) {
				--parent.linksCount;
			}

			if (!writeInode(parentInodeNumber, parent)) {
				return false;
			}

			const uint32_t group = (inodeNumber - 1) / superblock.inodesPerGroup;

			if (group < groupDescriptors.size() and groupDescriptors[group].usedDirsCountLo > 0) {
				--groupDescriptors[group].usedDirsCountLo;

				if (!writeGroupDescriptor(group)) {
					return false;
				}
			}
		}

		inode = Ext2Inode();

		if (!writeInode(inodeNumber, inode)) {
			return false;
		}

		return freeInode(inodeNumber) and flushDevice(device.deviceId);
	}

	auto Ext2Volume::renameFile(const string &oldPath, const string &newPath) -> bool {
		if (oldPath == newPath) {
			return true;
		}

		string oldParentPath;
		string oldName;
		string newParentPath;
		string newName;

		if (!splitParentPath(oldPath, oldParentPath, oldName) or !splitParentPath(newPath, newParentPath, newName) or
		    oldName == "." or oldName == ".." or newName == "." or newName == "..") {
			return false;
		}

		uint32_t existingNumber = 0;
		Ext2Inode existing {};

		if (lookupPath(newPath, existingNumber, existing)) {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext2Inode inode {};

		if (!lookupPath(oldPath, inodeNumber, inode) or inodeNumber == EXT2_ROOT_INO) {
			return false;
		}

		const uint16_t nodeMode = inode.mode & EXT2_S_IFMT;

		if (nodeMode != EXT2_S_IFREG and nodeMode != EXT2_S_IFDIR) {
			return false;
		}

		if (nodeMode == EXT2_S_IFDIR and (newParentPath == oldPath or newParentPath.starts_with(oldPath + "/"))) {
			return false;
		}

		uint32_t newParentInodeNumber = 0;
		Ext2Inode newParent {};

		if (!lookupPath(newParentPath, newParentInodeNumber, newParent) or (newParent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		uint32_t oldParentInodeNumber = 0;
		Ext2Inode oldParent {};

		if (!lookupPath(oldParentPath, oldParentInodeNumber, oldParent) or (oldParent.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		const bool crossParent = oldParentInodeNumber != newParentInodeNumber;
		const uint8_t fileType = nodeMode == EXT2_S_IFDIR ? 2 : 1;

		if (nodeMode == EXT2_S_IFDIR and crossParent) {
			++newParent.linksCount;
		}

		if (!addDirectoryEntry(newParentInodeNumber, newParent, inodeNumber, newName, fileType)) {
			return false;
		}

		uint32_t removedInode = 0;

		if (!removeDirectoryEntry(oldParentInodeNumber, oldParent, oldName, removedInode) or removedInode != inodeNumber) {
			removeDirectoryEntry(newParentInodeNumber, newParent, newName, removedInode);

			return false;
		}

		if (nodeMode == EXT2_S_IFDIR and crossParent) {
			if (oldParent.linksCount > 0) {
				--oldParent.linksCount;
			}

			if (!updateDirectoryEntryInode(inode, "..", newParentInodeNumber) or !writeInode(inodeNumber, inode) or
			    !writeInode(oldParentInodeNumber, oldParent) or !writeInode(newParentInodeNumber, newParent)) {
				return false;
			}
		}

		return flushDevice(device.deviceId);
	}

	auto Ext2Volume::findFirstRootTextFile(uint32_t &inodeNumber, string &name) const -> bool {
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

	auto Ext2Volume::readDirectory(const Ext2Inode &dir, vector<VfsDirEntry> &entries, const uint32_t startOffset, bool *hasMore, uint32_t *nextOffset) const -> bool {
		if ((dir.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
			return false;
		}

		vector<uint8_t> dirBytes;

		if (!readFile(dir, dirBytes, inodeFileSize(superblock, dir))) {
			return false;
		}

		entries.clear();
		uint64_t offset = startOffset;

		if (offset >= dirBytes.size()) {
			if (hasMore != nullptr) {
				*hasMore = false;
			}

			if (nextOffset != nullptr) {
				*nextOffset = static_cast<uint32_t>(dirBytes.size());
			}

			return true;
		}

		while (offset + 8 <= dirBytes.size() and entries.size() < VFS_MAX_DIR_ENTRIES) {
			const auto *entry = reinterpret_cast<const Ext2DirEntry *>(dirBytes.data() + offset);

			if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
				break;
			}

			if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8) {
				const string name(entry->name, entry->nameLen);

				if (name != "." and name != "..") {
					Ext2Inode child {};

					if (readInode(entry->inode, child)) {
						auto &out = entries.emplace_back();

						fillName(out.name, sizeof(out.name), out.nameLength, name);
						out.nodeType = inodeNodeType(child);
						out.size = inodeFileSize(superblock, child);
						out.nodeId = entry->inode;
					}
				}
			}

			offset += entry->recLen;
		}

		if (hasMore != nullptr) {
			*hasMore = offset + 8 <= dirBytes.size();
		}

		if (nextOffset != nullptr) {
			*nextOffset = static_cast<uint32_t>(offset);
		}

		return true;
	}

	auto Ext2Volume::lookupPath(const string &path, uint32_t &inodeNumber, Ext2Inode &inode) const -> bool {
		inodeNumber = EXT2_ROOT_INO;

		if (!readInode(inodeNumber, inode)) {
			return false;
		}

		for (const string &part : splitPath(path)) {
			if (part == "." or part.empty()) {
				continue;
			}

			if (part == ".." or (inode.mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
				return false;
			}

			vector<uint8_t> dirBytes;

			if (!readFile(inode, dirBytes, inodeFileSize(superblock, inode))) {
				return false;
			}

			bool found = false;
			uint64_t offset = 0;

			while (offset + 8 <= dirBytes.size()) {
				const auto *entry = reinterpret_cast<const Ext2DirEntry *>(dirBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
					break;
				}

				if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8 and string(entry->name, entry->nameLen) == part) {
					inodeNumber = entry->inode;
					found = readInode(inodeNumber, inode);
					break;
				}

				offset += entry->recLen;
			}

			if (!found) {
				return false;
			}
		}

		return true;
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

		const string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());

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

	auto Ext2Volume::fileSize(const Ext2Inode &inode) const -> uint64_t {
		return inodeFileSize(superblock, inode);
	}

	auto Ext2Volume::readBlock(const uint64_t fsBlock, uint8_t *buffer) const -> bool {
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

	auto Ext2Volume::writeBlock(const uint64_t fsBlock, const uint8_t *buffer) const -> bool {
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

	auto Ext2Volume::readIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto Ext2Volume::readDoubleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto Ext2Volume::readTripleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto mountedDevice(const uint64_t mountId, StorageFsProbeDeviceMsgData &device) -> bool {
		for (const auto &mount : mounts) {
			if (mount.mountId == mountId) {
				device = mount.device;

				return true;
			}
		}

		return false;
	}

	auto validPath(const char *path, const size_t length, string &out) -> bool {
		if (length == 0 or length > VFS_MAX_PATH_LENGTH or path[length - 1] != '\0') {
			return false;
		}

		out.assign(path, length - 1);

		return true;
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMountReplyMsgData();
			string deviceName;

			if (data.deviceId != 0 and data.blockCount != 0 and data.blockSize != 0 and validName(data.deviceName, data.deviceNameLength, sizeof(data.deviceName), deviceName)) {
				StorageFsProbeDeviceMsgData device {};

				device.deviceId = data.deviceId;
				device.blockCount = data.blockCount;
				device.blockSize = data.blockSize;
				fillName(device.deviceName, sizeof(device.deviceName), device.deviceNameLength, deviceName);

				Ext2Volume volume(device);

				if (volume.load()) {
					const uint64_t mountId = nextMountId++;

					mounts.push_back(MountedExt2 { .mountId = mountId, .device = device });
					reply.success = true;
					reply.mountId = mountId;

					printf("Ext2: Mounted %s as mount %lu.", device.deviceName, mountId);
					fflush(stdout);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_MOUNT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsStatReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);
				Ext2Inode inode {};
				uint32_t inodeNumber = 0;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode)) {
					reply.success = true;
					reply.nodeType = inodeNodeType(inode);
					reply.size = volume.fileSize(inode);
					reply.status = VFS_STATUS_OK;
					reply.nodeId = inodeNumber;
				} else {
					reply.status = VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_STAT_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadDirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);
				Ext2Inode inode {};
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
					reply.status = VFS_STATUS_NOT_FOUND;
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_READDIR_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);
				Ext2Inode inode {};
				uint32_t inodeNumber = 0;
				vector<uint8_t> bytes;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode) and inodeNodeType(inode) == VFS_NODE_FILE and volume.readFileRange(inode, data.offset, data.length, bytes)) {
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsWriteReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.length <= VFS_MAX_READ_SIZE and mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);
				Ext2Inode inode {};
				uint32_t inodeNumber = 0;

				if (volume.load() and volume.lookupPath(path, inodeNumber, inode) and inodeNodeType(inode) == VFS_NODE_FILE and volume.writeFile(inodeNumber, inode, data.offset, data.data, data.length)) {
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsCreateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (data.nodeType == VFS_NODE_FILE and mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.createFile(path);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext2Inode inode {};
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsMkdirReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.createDirectory(path);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext2Inode inode {};
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsSyncReplyMsgData();
			StorageFsProbeDeviceMsgData device {};

			if (mountedDevice(data.mountId, device)) {
				reply.success = flushDevice(device.deviceId);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;
			} else {
				reply.status = VFS_STATUS_NOT_FOUND;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_SYNC_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsLinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string oldPath;
			string newPath;

			if (mountedDevice(data.mountId, device) and validPath(data.oldPath, data.oldPathLength, oldPath) and validPath(data.newPath, data.newPathLength, newPath)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.createHardLink(oldPath, newPath);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext2Inode inode {};
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsSymlinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string target;
			string linkPath;

			if (mountedDevice(data.mountId, device) and validPath(data.target, data.targetLength, target) and validPath(data.linkPath, data.linkPathLength, linkPath)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.createSymlink(target, linkPath);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					Ext2Inode inode {};
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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsReadLinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;
			string target;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.readSymlink(path, target);
				reply.status = reply.success ? VFS_STATUS_OK : VFS_STATUS_INVALID;

				if (reply.success) {
					fillName(reply.target, sizeof(reply.target), reply.targetLength, target);
				}
			} else {
				reply.status = VFS_STATUS_INVALID;
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_READLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsUnlinkReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.unlinkFile(path);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_UNLINK_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsRenameReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string oldPath;
			string newPath;

			if (mountedDevice(data.mountId, device) and validPath(data.oldPath, data.oldPathLength, oldPath) and validPath(data.newPath, data.newPathLength, newPath)) {
				Ext2Volume volume(device);

				reply.success = volume.load() and volume.renameFile(oldPath, newPath);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = FS_RENAME_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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

			if (receive_horizonos_message(ext2Port, &msg, &filter) != 0) {
				continue;
			}

			auto reply = FsTruncateReplyMsgData();
			StorageFsProbeDeviceMsgData device {};
			string path;

			if (mountedDevice(data.mountId, device) and validPath(data.path, data.pathLength, path)) {
				Ext2Volume volume(device);

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

			send_horizonos_message(ext2Port, msg.src_port, &replyMsg);
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
		printf("Ext2: Failed to create message handlers.");
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
