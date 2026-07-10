#include "Ext4.hpp"
#include "Service.hpp"

#include <cstring>

using namespace std;

namespace horizonos::services::ext4 {
	Ext4Volume::Ext4Volume(const StorageFsProbeDeviceMsgData &device)
		: device(device) {
		pthread_mutex_lock(&service.volumeLock);
	}

	Ext4Volume::~Ext4Volume() {
		pthread_mutex_unlock(&service.volumeLock);
	}

	auto Ext4Volume::load() -> bool {
		ioFailed = false;

		if (device.blockSize == 0 or device.blockSize > 4096 or (4096 % device.blockSize) != 0) {
			return false;
		}

		const uint64_t superLba = EXT4_SUPERBLOCK_OFFSET / device.blockSize;
		const uint64_t superOffsetInPage = EXT4_SUPERBLOCK_OFFSET - (superLba * device.blockSize);
		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!Utils::readDevicePage(device.deviceId, superLba, phys, virt)) {
			ioFailed = true;
			return false;
		}

		memcpy(&superblock, reinterpret_cast<const void *>(virt + superOffsetInPage), sizeof(superblock));
		Utils::freeDevicePage(phys, virt);

		if (superblock.magic != EXT4_SUPER_MAGIC) {
			return false;
		}

		const uint32_t unsupportedIncompat = superblock.featureIncompat & ~EXT4_SUPPORTED_INCOMPAT_FEATURES;

		if (unsupportedIncompat != 0) {
			printf("Ext4: %s has unsupported incompatible feature flags 0x%x.", device.deviceName, unsupportedIncompat);
			fflush(stdout);

			return false;
		}

		if (superblock.logBlockSize > 2) {
			return false;
		}

		blockSize = 1024ULL << superblock.logBlockSize;

		if (blockSize < 1024 or blockSize > 4096 or blockSize % device.blockSize != 0) {
			return false;
		}

		const uint16_t inodeSize = Utils::inodeSize(superblock);

		if (!ext4_rules::validInodeSize(inodeSize, blockSize)) {
			return false;
		}

		const uint64_t totalBlocks = Utils::blocksCount(superblock);

		if (totalBlocks <= superblock.firstDataBlock or superblock.blocksPerGroup == 0 or superblock.inodesPerGroup == 0 or
		    totalBlocks > UINT64_MAX / blockSize or device.blockCount > UINT64_MAX / device.blockSize or
		    totalBlocks * blockSize > device.blockCount * static_cast<uint64_t>(device.blockSize)) {
			return false;
		}

		if (!ext4_rules::selectGroupDescriptorSize((superblock.featureIncompat & EXT4_FEATURE_INCOMPAT_64BIT) != 0,
		                                               superblock.descSize,
		                                               blockSize,
		                                               groupDescriptorSize)) {
			return false;
		}

		const uint64_t groupCount = ext4_rules::blockGroupCount(totalBlocks, superblock.firstDataBlock, superblock.blocksPerGroup);
		const uint16_t descSize = groupDescriptorSize;

		if (groupCount > UINT64_MAX / descSize or groupCount > SIZE_MAX / sizeof(Ext4GroupDescriptor)) {
			return false;
		}

		const uint64_t descTableBlock = superblock.firstDataBlock + 1;
		const uint64_t descTableBytes = groupCount * descSize;
		const uint64_t descTableBlocks = ((descTableBytes - 1) / blockSize) + 1;
		vector<uint8_t> descBytes;

		if (descTableBlocks > SIZE_MAX / blockSize) {
			return false;
		}

		descBytes.resize(descTableBlocks * blockSize);

		for (uint64_t i = 0; i < descTableBlocks; ++i) {
			if (!readBlock(descTableBlock + i, descBytes.data() + (i * blockSize))) {
				return false;
			}
		}

		groupDescriptors.clear();
		groupDescriptors.resize(groupCount);

		for (uint64_t group = 0; group < groupCount; ++group) {
			memcpy(&groupDescriptors[group], descBytes.data() + (group * descSize), min<size_t>(descSize, sizeof(Ext4GroupDescriptor)));
		}

		return true;
	}

	auto Ext4Volume::readInode(const uint32_t inodeNumber, Ext4Inode &out) const -> bool {
			if (inodeNumber == 0 or inodeNumber > superblock.inodesCount) {
				return false;
			}

			const uint32_t index = inodeNumber - 1;
			const uint32_t group = index / superblock.inodesPerGroup;
			const uint32_t indexInGroup = index % superblock.inodesPerGroup;

			if (group >= groupDescriptors.size()) {
				return false;
			}

			const uint64_t tableBlock = Utils::groupDescriptorInodeTable(superblock, groupDescriptors[group]);
			const uint64_t byteOffset = static_cast<uint64_t>(indexInGroup) * Utils::inodeSize(superblock);
			const uint64_t block = tableBlock + (byteOffset / blockSize);
			const uint64_t offset = byteOffset % blockSize;
			vector<uint8_t> blockBytes;

			blockBytes.resize(blockSize);

			if (!readBlock(block, blockBytes.data())) {
				return false;
			}

			memcpy(&out, blockBytes.data() + offset, min<size_t>(sizeof(out), Utils::inodeSize(superblock)));

			return true;
		}

	auto Ext4Volume::writeInode(const uint32_t inodeNumber, const Ext4Inode &inode) const -> bool {
		if (inodeNumber == 0 or inodeNumber > superblock.inodesCount) {
			return false;
		}

		const uint32_t index = inodeNumber - 1;
		const uint32_t group = index / superblock.inodesPerGroup;
		const uint32_t indexInGroup = index % superblock.inodesPerGroup;

		if (group >= groupDescriptors.size()) {
			return false;
		}

		const uint64_t tableBlock = Utils::groupDescriptorInodeTable(superblock, groupDescriptors[group]);
		const uint64_t byteOffset = static_cast<uint64_t>(indexInGroup) * Utils::inodeSize(superblock);
		const uint64_t block = tableBlock + (byteOffset / blockSize);
		const uint64_t offset = byteOffset % blockSize;
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		if (!readBlock(block, blockBytes.data())) {
			return false;
		}

		memcpy(blockBytes.data() + offset, &inode, min<size_t>(sizeof(inode), Utils::inodeSize(superblock)));

		return writeBlock(block, blockBytes.data());
	}

	auto Ext4Volume::inodeUsesExtents(const Ext4Inode &inode) -> bool {
		return (inode.flags & EXT4_EXTENTS_FL) != 0;
	}

	auto Ext4Volume::mutationsSupported() const -> bool {
		return (superblock.featureIncompat & EXT4_UNSUPPORTED_WRITE_INCOMPAT_FEATURES) == 0 and
			(superblock.featureRoCompat & EXT4_UNSUPPORTED_WRITE_RO_COMPAT_FEATURES) == 0;
	}

	auto Ext4Volume::resolveDataBlock(const Ext4Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) const -> bool {
		fsBlock = 0;

		if (inodeUsesExtents(inode)) {
			return resolveExtentBlock(inode, fileBlock, fsBlock);
		}

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

	auto Ext4Volume::readFile(const Ext4Inode &inode, vector<uint8_t> &out, const uint64_t maxBytes) const -> bool {
		const uint64_t fileSize = Utils::inodeFileSize(superblock, inode);
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
			} else if (ioFailed) {
				return false;
			} else {
				memset(out.data() + copied, 0, toCopy);
			}

			copied += toCopy;
			++fileBlock;
		}

		return true;
	}

	auto Ext4Volume::readFileRange(const Ext4Inode &inode, const uint64_t offset, const uint32_t length, vector<uint8_t> &out) const -> bool {
		const uint64_t fileSize = Utils::inodeFileSize(superblock, inode);

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
			} else if (ioFailed) {
				return false;
			} else {
				memset(out.data() + copied, 0, toCopy);
			}

			copied += toCopy;
		}

		return true;
	}

	auto Ext4Volume::writeFileOverwrite(const uint32_t inodeNumber, const Ext4Inode &inode, const uint64_t offset, const uint8_t *data, const uint64_t length) const -> bool {
		(void) inodeNumber;

		if (!mutationsSupported()) {
			return false;
		}

		const uint64_t fileSize = Utils::inodeFileSize(superblock, inode);

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

		return Utils::flushDevice(device.deviceId);
	}

	void Ext4Volume::setInodeFileSize(Ext4Inode &inode, const uint64_t size) {
		inode.sizeLo = static_cast<uint32_t>(size);

		if ((inode.mode & EXT4_S_IFMT) == EXT4_S_IFREG) {
			inode.sizeHighOrDirAcl = static_cast<uint32_t>(size >> 32);
		}
	}

	auto Ext4Volume::writeSuperblock() const -> bool {
		const uint64_t superLba = EXT4_SUPERBLOCK_OFFSET / device.blockSize;
		const uint64_t superOffsetInPage = EXT4_SUPERBLOCK_OFFSET - (superLba * device.blockSize);
		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!Utils::readDevicePage(device.deviceId, superLba, phys, virt)) {
			return false;
		}

		memcpy(reinterpret_cast<void *>(virt + superOffsetInPage), &superblock, sizeof(superblock));

		const bool success = Utils::writeDevicePage(device.deviceId, superLba, phys);

		Utils::freeDevicePage(phys, virt);

		return success;
	}

	auto Ext4Volume::writeGroupDescriptor(const uint32_t group) const -> bool {
		if (group >= groupDescriptors.size()) {
			return false;
		}

		const uint64_t descTableBlock = superblock.firstDataBlock + 1;
		const uint64_t offset = static_cast<uint64_t>(group) * groupDescriptorSize;
		const uint64_t block = descTableBlock + (offset / blockSize);
		const uint64_t blockOffset = offset % blockSize;
		vector<uint8_t> bytes;

		bytes.resize(blockSize);

		if (!readBlock(block, bytes.data())) {
			return false;
		}

		memcpy(bytes.data() + blockOffset, &groupDescriptors[group], min<size_t>(groupDescriptorSize, sizeof(Ext4GroupDescriptor)));

		return writeBlock(block, bytes.data());
	}

	auto Ext4Volume::allocateBlock(uint32_t &block) -> bool {
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

				if (candidate == 0 or candidate >= Utils::blocksCount(superblock)) {
					break;
				}

				if (Utils::bitmapBitSet(bitmap, bit)) {
					continue;
				}

				Utils::bitmapSetBit(bitmap, bit);
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

	auto Ext4Volume::allocateInode(uint32_t &inodeNumber) -> bool {
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

				if (Utils::bitmapBitSet(bitmap, bit)) {
					continue;
				}

				Utils::bitmapSetBit(bitmap, bit);
				inodeNumber = static_cast<uint32_t>(candidate);

				--superblock.freeInodesCount;
				--groupDescriptors[group].freeInodesCountLo;

				return writeBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
			}
		}

		return false;
	}

	auto Ext4Volume::freeBlock(const uint32_t block) -> bool {
		if (block < superblock.firstDataBlock or block >= Utils::blocksCount(superblock)) {
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

		if (!Utils::bitmapBitSet(bitmap, bit)) {
			return true;
		}

		Utils::bitmapClearBit(bitmap, bit);
		++superblock.freeBlocksCount;
		++groupDescriptors[group].freeBlocksCountLo;

		return writeBlock(groupDescriptors[group].blockBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
	}

	auto Ext4Volume::freeInode(const uint32_t inodeNumber) -> bool {
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

		if (!Utils::bitmapBitSet(bitmap, bit)) {
			return true;
		}

		Utils::bitmapClearBit(bitmap, bit);
		++superblock.freeInodesCount;
		++groupDescriptors[group].freeInodesCountLo;

		return writeBlock(groupDescriptors[group].inodeBitmapLo, bitmap.data()) and writeGroupDescriptor(group) and writeSuperblock();
	}

	auto Ext4Volume::ensureIndirectDataBlock(Ext4Inode &inode, uint32_t &pointerBlock, const uint32_t depth, const uint64_t index, uint32_t &fsBlock) -> bool {
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

	auto Ext4Volume::ensureDataBlock(Ext4Inode &inode, uint64_t fileBlock, uint32_t &fsBlock) -> bool {
		fsBlock = 0;

		if (!mutationsSupported()) {
			return false;
		}

		if (inodeUsesExtents(inode)) {
			return false;
		}

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

	auto Ext4Volume::appendExtentToLeaf(uint8_t *leaf, const uint64_t fileBlock, const uint32_t allocatedBlock) -> bool {
		auto *header = reinterpret_cast<Ext4ExtentHeader *>(leaf);

		if (header->magic != EXT4_EXT_MAGIC or header->depth != 0 or header->entries > header->max) {
			return false;
		}

		auto *extents = reinterpret_cast<Ext4Extent *>(leaf + sizeof(Ext4ExtentHeader));

		if (header->entries != 0) {
			auto &last = extents[header->entries - 1];
			const uint32_t lastLen = Utils::extentLength(last);
			const uint64_t lastStart = Utils::extentStartBlock(last);

			if (fileBlock < static_cast<uint64_t>(last.block) + lastLen) {
				return false;
			}

			if (static_cast<uint64_t>(last.block) + lastLen == fileBlock and lastStart + lastLen == allocatedBlock and lastLen < 0x7fffU) {
				last.len = static_cast<uint16_t>((last.len & 0x8000U) | (lastLen + 1));

				return true;
			}
		}

		if (header->entries >= header->max) {
			return false;
		}

		auto &extent = extents[header->entries++];

		extent.block = static_cast<uint32_t>(fileBlock);
		extent.len = 1;
		extent.startHi = static_cast<uint16_t>(static_cast<uint64_t>(allocatedBlock) >> 32);
		extent.startLo = allocatedBlock;

		return true;
	}

	auto Ext4Volume::ensureExtentDataBlock(Ext4Inode &inode, const uint64_t fileBlock, uint32_t &fsBlock) -> bool {
		fsBlock = 0;

		if (!mutationsSupported() or !inodeUsesExtents(inode) or fileBlock > 0xffffffffULL) {
			return false;
		}

		uint64_t existing = 0;

		if (resolveExtentBlock(inode, fileBlock, existing)) {
			fsBlock = static_cast<uint32_t>(existing);

			return existing <= 0xffffffffULL;
		}

		auto *header = reinterpret_cast<Ext4ExtentHeader *>(inode.block);
		uint32_t allocated = 0;

		if (!allocateBlock(allocated)) {
			return false;
		}

		if (header->magic != EXT4_EXT_MAGIC or header->entries > header->max) {
			freeBlock(allocated);
			return false;
		}

		if (header->depth == 0) {
			if (!appendExtentToLeaf(reinterpret_cast<uint8_t *>(inode.block), fileBlock, allocated)) {
				freeBlock(allocated);
				return false;
			}

			fsBlock = allocated;
			inode.blocks += static_cast<uint32_t>(blockSize / 512);

			return true;
		}

		if (header->depth != 1) {
			freeBlock(allocated);
			return false;
		}

		auto *indices = reinterpret_cast<Ext4ExtentIndex *>(reinterpret_cast<uint8_t *>(inode.block) + sizeof(Ext4ExtentHeader));
		const Ext4ExtentIndex *selected = nullptr;

		for (uint16_t i = 0; i < header->entries; ++i) {
			if (fileBlock < indices[i].block) {
				break;
			}

			selected = &indices[i];
		}

		if (selected != nullptr) {
			vector<uint8_t> leaf;

			leaf.resize(blockSize);

			if (!readBlock(Utils::extentIndexLeafBlock(*selected), leaf.data())) {
				freeBlock(allocated);
				return false;
			}

			if (appendExtentToLeaf(leaf.data(), fileBlock, allocated)) {
				if (!writeBlock(Utils::extentIndexLeafBlock(*selected), leaf.data())) {
					freeBlock(allocated);
					return false;
				}

				fsBlock = allocated;
				inode.blocks += static_cast<uint32_t>(blockSize / 512);

				return true;
			}
		}

		if (header->entries >= header->max or (header->entries != 0 and fileBlock <= indices[header->entries - 1].block)) {
			freeBlock(allocated);
			return false;
		}

		uint32_t leafBlock = 0;

		if (!allocateBlock(leafBlock)) {
			freeBlock(allocated);
			return false;
		}

		vector<uint8_t> leaf;

		leaf.resize(blockSize);

		auto *leafHeader = reinterpret_cast<Ext4ExtentHeader *>(leaf.data());

		leafHeader->magic = EXT4_EXT_MAGIC;
		leafHeader->entries = 0;
		leafHeader->max = static_cast<uint16_t>((blockSize - sizeof(Ext4ExtentHeader)) / sizeof(Ext4Extent));
		leafHeader->depth = 0;
		leafHeader->generation = 0;

		if (!appendExtentToLeaf(leaf.data(), fileBlock, allocated) or !writeBlock(leafBlock, leaf.data())) {
			freeBlock(allocated);
			freeBlock(leafBlock);
			return false;
		}

		auto &index = indices[header->entries++];

		index.block = static_cast<uint32_t>(fileBlock);
		index.leafLo = leafBlock;
		index.leafHi = static_cast<uint16_t>(static_cast<uint64_t>(leafBlock) >> 32);
		index.unused = 0;
		fsBlock = allocated;
		inode.blocks += static_cast<uint32_t>((blockSize / 512) * 2);

		return true;
	}

	auto Ext4Volume::writeFile(const uint32_t inodeNumber, Ext4Inode &inode, const uint64_t offset, const uint8_t *data, const uint64_t length) -> bool {
		if ((inode.mode & EXT4_S_IFMT) != EXT4_S_IFREG or offset > (1ULL << 47)) {
			return false;
		}

		if (!mutationsSupported()) {
			return false;
		}

		if (length == 0) {
			return true;
		}

		const uint64_t newSize = max<uint64_t>(Utils::inodeFileSize(superblock, inode), offset + length);
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		uint64_t written = 0;

		while (written < length) {
			const uint64_t fileOffset = offset + written;
			const uint64_t fileBlock = fileOffset / blockSize;
			const uint64_t blockOffset = fileOffset % blockSize;
			const uint64_t toCopy = min<uint64_t>(blockSize - blockOffset, length - written);
			uint32_t fsBlock = 0;

			if (!(inodeUsesExtents(inode) ? ensureExtentDataBlock(inode, fileBlock, fsBlock) : ensureDataBlock(inode, fileBlock, fsBlock))) {
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

		if (newSize > 0xffffffffULL and (superblock.featureRoCompat & EXT4_FEATURE_RO_COMPAT_LARGE_FILE) == 0) {
			superblock.featureRoCompat |= EXT4_FEATURE_RO_COMPAT_LARGE_FILE;

			if (!writeSuperblock()) {
				return false;
			}
		}

		setInodeFileSize(inode, newSize);

		return writeInode(inodeNumber, inode) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::splitParentPath(const string &path, string &parentPath, string &name) -> bool {
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

	auto Ext4Volume::addDirectoryEntry(const uint32_t parentInodeNumber, Ext4Inode &parent, const uint32_t childInodeNumber, const string &name, const uint8_t fileType) -> bool {
		if ((parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR or name.empty() or name.size() > 255) {
			return false;
		}

		const uint16_t needed = Utils::ext4DirRecLen(name.size());
		const uint64_t parentSize = Utils::inodeFileSize(superblock, parent);
		const uint64_t blocks = max<uint64_t>(1, (parentSize + blockSize - 1) / blockSize);
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		for (uint64_t fileBlock = 0; fileBlock < blocks; ++fileBlock) {
			uint64_t resolvedBlock = 0;

			if (!resolveDataBlock(parent, fileBlock, resolvedBlock) or resolvedBlock > UINT32_MAX) {
				return false;
			}

			const auto fsBlock = static_cast<uint32_t>(resolvedBlock);

			if (!readBlock(fsBlock, blockBytes.data())) {
				return false;
			}

			uint64_t offset = 0;

			while (offset + 8 <= blockSize) {
				auto *entry = reinterpret_cast<Ext4DirEntry *>(blockBytes.data() + offset);

				if (entry->recLen < 8 or offset + entry->recLen > blockSize) {
					break;
				}

				const uint16_t actual = entry->inode == 0 ? 8 : Utils::ext4DirRecLen(entry->nameLen);
				const uint16_t available = entry->recLen;

				if (available >= actual + needed) {
					entry->recLen = actual;

					auto *newEntry = reinterpret_cast<Ext4DirEntry *>(blockBytes.data() + offset + actual);

					newEntry->inode = childInodeNumber;
					newEntry->recLen = available - actual;
					newEntry->nameLen = static_cast<uint8_t>(name.size());
					newEntry->fileType = fileType;
					memcpy(newEntry->name, name.data(), name.size());

					return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and Utils::flushDevice(device.deviceId);
				}

				offset += entry->recLen;
			}
		}

		const uint64_t newFileBlock = blocks;
		uint32_t fsBlock = 0;

		if (!(inodeUsesExtents(parent)
			? ensureExtentDataBlock(parent, newFileBlock, fsBlock)
			: ensureDataBlock(parent, newFileBlock, fsBlock))) {
			return false;
		}

		memset(blockBytes.data(), 0, blockSize);

		auto *entry = reinterpret_cast<Ext4DirEntry *>(blockBytes.data());

		entry->inode = childInodeNumber;
		entry->recLen = static_cast<uint16_t>(blockSize);
		entry->nameLen = static_cast<uint8_t>(name.size());
		entry->fileType = fileType;
		memcpy(entry->name, name.data(), name.size());

		setInodeFileSize(parent, parentSize + blockSize);

		return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::removeDirectoryEntry(const uint32_t parentInodeNumber, Ext4Inode &parent, const string &name, uint32_t &removedInode) -> bool {
		removedInode = 0;

		if ((parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR or name.empty()) {
			return false;
		}

		const uint64_t parentSize = Utils::inodeFileSize(superblock, parent);
		const uint64_t blocks = (parentSize + blockSize - 1) / blockSize;
		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		for (uint64_t fileBlock = 0; fileBlock < blocks; ++fileBlock) {
			uint64_t fsBlock = 0;

			if (!resolveDataBlock(parent, fileBlock, fsBlock) or !readBlock(fsBlock, blockBytes.data())) {
				return false;
			}

			uint64_t offset = 0;
			Ext4DirEntry *previous = nullptr;

			while (offset + 8 <= blockSize) {
				auto *entry = reinterpret_cast<Ext4DirEntry *>(blockBytes.data() + offset);

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

					return writeBlock(fsBlock, blockBytes.data()) and writeInode(parentInodeNumber, parent) and Utils::flushDevice(device.deviceId);
				}

				if (entry->inode != 0) {
					previous = entry;
				}

				offset += entry->recLen;
			}
		}

		return false;
	}

	auto Ext4Volume::directoryIsEmpty(const Ext4Inode &dir) const -> bool {
		if ((dir.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		vector<uint8_t> dirBytes;

		if (!readFile(dir, dirBytes, Utils::inodeFileSize(superblock, dir))) {
			return false;
		}

		uint64_t offset = 0;

		while (offset + 8 <= dirBytes.size()) {
			const auto *entry = reinterpret_cast<const Ext4DirEntry *>(dirBytes.data() + offset);

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

	auto Ext4Volume::updateDirectoryEntryInode(const Ext4Inode &dir, const string &name, const uint32_t inodeNumber) const -> bool {
		if ((dir.mode & EXT4_S_IFMT) != EXT4_S_IFDIR or name.empty()) {
			return false;
		}

		const uint64_t dirSize = Utils::inodeFileSize(superblock, dir);
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
				auto *entry = reinterpret_cast<Ext4DirEntry *>(blockBytes.data() + offset);

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

	auto Ext4Volume::freeIndirectBlocks(uint32_t &pointerBlock, const uint32_t depth, const uint64_t keepBlocks, const uint64_t span) -> bool {
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

	auto Ext4Volume::freeInodeBlocks(Ext4Inode &inode, const uint64_t keepBlocks) -> bool {
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

	auto Ext4Volume::truncateExtentBlocks(Ext4Inode &inode, const uint64_t keepBlocks) -> bool {
		if (!inodeUsesExtents(inode)) {
			return false;
		}

		auto *header = reinterpret_cast<Ext4ExtentHeader *>(inode.block);

		if (header->magic != EXT4_EXT_MAGIC or header->depth != 0 or header->entries > header->max) {
			if (header->magic != EXT4_EXT_MAGIC or header->depth != 1 or header->entries > header->max) {
				return false;
			}

			auto *indices = reinterpret_cast<Ext4ExtentIndex *>(reinterpret_cast<uint8_t *>(inode.block) + sizeof(Ext4ExtentHeader));
			uint16_t out = 0;

			for (uint16_t i = 0; i < header->entries; ++i) {
				vector<uint8_t> leaf;
				const uint64_t leafBlock = Utils::extentIndexLeafBlock(indices[i]);

				leaf.resize(blockSize);

				if (!readBlock(leafBlock, leaf.data())) {
					return false;
				}

				if (!truncateExtentLeaf(leaf.data(), keepBlocks, inode)) {
					return false;
				}

				const auto *leafHeader = reinterpret_cast<const Ext4ExtentHeader *>(leaf.data());

				if (leafHeader->entries == 0) {
					if (!freeBlock(static_cast<uint32_t>(leafBlock))) {
						return false;
					}

					if (inode.blocks >= blockSize / 512) {
						inode.blocks -= static_cast<uint32_t>(blockSize / 512);
					}

					continue;
				}

				const auto *leafExtents = reinterpret_cast<const Ext4Extent *>(leaf.data() + sizeof(Ext4ExtentHeader));

				indices[i].block = leafExtents[0].block;

				if (!writeBlock(leafBlock, leaf.data())) {
					return false;
				}

				indices[out++] = indices[i];
			}

			header->entries = out;

			for (uint16_t i = out; i < header->max; ++i) {
				indices[i] = Ext4ExtentIndex();
			}

			return true;
		}

		return truncateExtentLeaf(reinterpret_cast<uint8_t *>(inode.block), keepBlocks, inode);
	}

	auto Ext4Volume::truncateExtentLeaf(uint8_t *leaf, const uint64_t keepBlocks, Ext4Inode &inode) -> bool {
		auto *header = reinterpret_cast<Ext4ExtentHeader *>(leaf);

		if (header->magic != EXT4_EXT_MAGIC or header->depth != 0 or header->entries > header->max) {
			return false;
		}

		auto *extents = reinterpret_cast<Ext4Extent *>(leaf + sizeof(Ext4ExtentHeader));
		uint16_t out = 0;

		for (uint16_t i = 0; i < header->entries; ++i) {
			auto extent = extents[i];
			const uint32_t len = Utils::extentLength(extent);
			const uint64_t start = Utils::extentStartBlock(extent);
			const uint64_t firstFileBlock = extent.block;
			const uint64_t endFileBlock = firstFileBlock + len;

			if (len == 0 or firstFileBlock >= keepBlocks) {
				for (uint32_t block = 0; block < len; ++block) {
					if (!freeBlock(static_cast<uint32_t>(start + block))) {
						return false;
					}

					if (inode.blocks >= blockSize / 512) {
						inode.blocks -= static_cast<uint32_t>(blockSize / 512);
					}
				}

				continue;
			}

			if (endFileBlock > keepBlocks) {
				const auto keepLen = static_cast<uint32_t>(keepBlocks - firstFileBlock);

				for (uint32_t block = keepLen; block < len; ++block) {
					if (!freeBlock(static_cast<uint32_t>(start + block))) {
						return false;
					}

					if (inode.blocks >= blockSize / 512) {
						inode.blocks -= static_cast<uint32_t>(blockSize / 512);
					}
				}

				extent.len = static_cast<uint16_t>((extent.len & 0x8000U) | keepLen);
			}

			extents[out++] = extent;
		}

		header->entries = out;

		for (uint16_t i = out; i < header->max; ++i) {
			extents[i] = Ext4Extent();
		}

		return true;
	}

	auto Ext4Volume::countIndirectBlocks(const uint32_t pointerBlock, const uint32_t depth) const -> uint64_t {
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

	auto Ext4Volume::countInodeBlocks(const Ext4Inode &inode) const -> uint64_t {
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

	auto Ext4Volume::createFile(const string &path) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t existingInode = 0;
		Ext4Inode existing {};

		if (lookupPath(path, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext4Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		auto inode = Ext4Inode();

		inode.mode = EXT4_S_IFREG | 0644;
		inode.linksCount = 1;

		if ((superblock.featureIncompat & EXT4_FEATURE_INCOMPAT_EXTENTS) != 0) {
			inode.flags |= EXT4_EXTENTS_FL;

			auto *header = reinterpret_cast<Ext4ExtentHeader *>(inode.block);

			header->magic = EXT4_EXT_MAGIC;
			header->entries = 0;
			header->max = static_cast<uint16_t>((sizeof(inode.block) - sizeof(Ext4ExtentHeader)) / sizeof(Ext4Extent));
			header->depth = 0;
			header->generation = 0;
		}

		if (!writeInode(inodeNumber, inode)) {
			freeInode(inodeNumber);
			return false;
		}

		if (!addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, 1)) {
			freeInode(inodeNumber);
			return false;
		}

		return Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::createDirectory(const string &path) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t existingInode = 0;
		Ext4Inode existing {};

		if (lookupPath(path, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext4Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		auto inode = Ext4Inode();

		inode.mode = EXT4_S_IFDIR | 0755;
		inode.linksCount = 2;
		inode.sizeLo = static_cast<uint32_t>(blockSize);

		uint32_t dirBlock = 0;

		if ((superblock.featureIncompat & EXT4_FEATURE_INCOMPAT_EXTENTS) != 0) {
			inode.flags |= EXT4_EXTENTS_FL;

			auto *header = reinterpret_cast<Ext4ExtentHeader *>(inode.block);
			header->magic = EXT4_EXT_MAGIC;
			header->entries = 0;
			header->max = static_cast<uint16_t>((sizeof(inode.block) - sizeof(Ext4ExtentHeader)) / sizeof(Ext4Extent));
			header->depth = 0;
			header->generation = 0;

			if (!ensureExtentDataBlock(inode, 0, dirBlock)) {
				freeInode(inodeNumber);
				return false;
			}
		} else {
			if (!allocateBlock(dirBlock)) {
				freeInode(inodeNumber);
				return false;
			}

			inode.blocks = static_cast<uint32_t>(blockSize / 512);
			inode.block[0] = dirBlock;
		}

		vector<uint8_t> blockBytes;

		blockBytes.resize(blockSize);

		auto *dot = reinterpret_cast<Ext4DirEntry *>(blockBytes.data());

		dot->inode = inodeNumber;
		dot->recLen = Utils::ext4DirRecLen(1);
		dot->nameLen = 1;
		dot->fileType = 2;
		dot->name[0] = '.';

		auto *dotDot = reinterpret_cast<Ext4DirEntry *>(blockBytes.data() + dot->recLen);

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

		return writeInode(parentInodeNumber, parent) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::createHardLink(const string &oldPath, const string &newPath) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		string parentPath;
		string name;

		if (!splitParentPath(newPath, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t existingInode = 0;
		Ext4Inode existing {};

		if (lookupPath(newPath, existingInode, existing)) {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext4Inode inode {};

		if (!lookupPath(oldPath, inodeNumber, inode) or (inode.mode & EXT4_S_IFMT) == EXT4_S_IFDIR) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext4Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		if (!addDirectoryEntry(parentInodeNumber, parent, inodeNumber, name, Utils::ext4DirectoryFileType(inode))) {
			return false;
		}

		++inode.linksCount;

		return writeInode(inodeNumber, inode) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::createSymlink(const string &target, const string &linkPath) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		string parentPath;
		string name;

		if (target.empty() or target.size() >= VFS_MAX_PATH_LENGTH or !splitParentPath(linkPath, parentPath, name)) {
			return false;
		}

		uint32_t existingInode = 0;
		Ext4Inode existing {};

		if (lookupPath(linkPath, existingInode, existing)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext4Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		uint32_t inodeNumber = 0;

		if (!allocateInode(inodeNumber)) {
			return false;
		}

		auto inode = Ext4Inode();

		inode.mode = EXT4_S_IFLNK | 0777;
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

	auto Ext4Volume::readSymlink(const string &linkPath, string &target) const -> bool {
		Ext4Inode inode {};
		uint32_t inodeNumber = 0;

		if (!lookupPath(linkPath, inodeNumber, inode) or Utils::inodeNodeType(inode) != VFS_NODE_SYMLINK) {
			return false;
		}

		(void) inodeNumber;

		const uint64_t size = Utils::inodeFileSize(superblock, inode);

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

	auto Ext4Volume::truncateFile(const string &path, const uint64_t size) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext4Inode inode {};

		if (!lookupPath(path, inodeNumber, inode) or (inode.mode & EXT4_S_IFMT) != EXT4_S_IFREG) {
			return false;
		}

		const uint64_t keepBlocks = (size + blockSize - 1) / blockSize;

		if (inodeUsesExtents(inode)) {
			const uint64_t currentSize = Utils::inodeFileSize(superblock, inode);

			if (size > currentSize) {
				for (uint64_t fileBlock = (currentSize + blockSize - 1) / blockSize; fileBlock < keepBlocks; ++fileBlock) {
					uint32_t fsBlock = 0;

					if (!ensureExtentDataBlock(inode, fileBlock, fsBlock)) {
						return false;
					}
				}
			} else if (!truncateExtentBlocks(inode, keepBlocks)) {
				return false;
			}
		} else if (size > 0) {
			for (uint64_t fileBlock = 0; fileBlock < keepBlocks; ++fileBlock) {
				uint32_t fsBlock = 0;

				if (!ensureDataBlock(inode, fileBlock, fsBlock)) {
					return false;
				}
			}

			if (!freeInodeBlocks(inode, keepBlocks)) {
				return false;
			}
		} else if (!freeInodeBlocks(inode, keepBlocks)) {
			return false;
		}

		if (size > 0xffffffffULL and (superblock.featureRoCompat & EXT4_FEATURE_RO_COMPAT_LARGE_FILE) == 0) {
			superblock.featureRoCompat |= EXT4_FEATURE_RO_COMPAT_LARGE_FILE;

			if (!writeSuperblock()) {
				return false;
			}
		}

		setInodeFileSize(inode, size);

		if (!inodeUsesExtents(inode)) {
			inode.blocks = static_cast<uint32_t>(countInodeBlocks(inode) * (blockSize / 512));
		}

		return writeInode(inodeNumber, inode) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::unlinkFile(const string &path) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

		string parentPath;
		string name;

		if (!splitParentPath(path, parentPath, name) or name == "." or name == "..") {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext4Inode inode {};

		if (!lookupPath(path, inodeNumber, inode) or inodeNumber == EXT4_ROOT_INO) {
			return false;
		}

		const uint16_t nodeMode = inode.mode & EXT4_S_IFMT;

		if (nodeMode != EXT4_S_IFREG and nodeMode != EXT4_S_IFDIR and nodeMode != EXT4_S_IFLNK and nodeMode != 0) {
			return false;
		}

		if (nodeMode == EXT4_S_IFDIR and !directoryIsEmpty(inode)) {
			return false;
		}

		uint32_t parentInodeNumber = 0;
		Ext4Inode parent {};

		if (!lookupPath(parentPath, parentInodeNumber, parent) or (parent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		uint32_t removedInode = 0;

		if (!removeDirectoryEntry(parentInodeNumber, parent, name, removedInode) or removedInode != inodeNumber) {
			return false;
		}

		if (nodeMode != EXT4_S_IFDIR and inode.linksCount > 1) {
			--inode.linksCount;

			return writeInode(inodeNumber, inode) and Utils::flushDevice(device.deviceId);
		}

		const bool fastSymlink = nodeMode == EXT4_S_IFLNK and inode.blocks == 0;

		if (!fastSymlink and inodeUsesExtents(inode) and !truncateExtentBlocks(inode, 0)) {
			return false;
		}

		if (!fastSymlink and !inodeUsesExtents(inode) and !freeInodeBlocks(inode, 0)) {
			return false;
		}

		if (nodeMode == EXT4_S_IFDIR) {
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

		inode = Ext4Inode();

		if (!writeInode(inodeNumber, inode)) {
			return false;
		}

		return freeInode(inodeNumber) and Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::renameFile(const string &oldPath, const string &newPath) -> bool {
		if (!mutationsSupported()) {
			return false;
		}

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
		Ext4Inode existing {};

		if (lookupPath(newPath, existingNumber, existing)) {
			return false;
		}

		uint32_t inodeNumber = 0;
		Ext4Inode inode {};

		if (!lookupPath(oldPath, inodeNumber, inode) or inodeNumber == EXT4_ROOT_INO) {
			return false;
		}

		const uint16_t nodeMode = inode.mode & EXT4_S_IFMT;

		if (nodeMode != EXT4_S_IFREG and nodeMode != EXT4_S_IFDIR) {
			return false;
		}

		if (nodeMode == EXT4_S_IFDIR and (newParentPath == oldPath or newParentPath.starts_with(oldPath + "/"))) {
			return false;
		}

		uint32_t newParentInodeNumber = 0;
		Ext4Inode newParent {};

		if (!lookupPath(newParentPath, newParentInodeNumber, newParent) or (newParent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		uint32_t oldParentInodeNumber = 0;
		Ext4Inode oldParent {};

		if (!lookupPath(oldParentPath, oldParentInodeNumber, oldParent) or (oldParent.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		const bool crossParent = oldParentInodeNumber != newParentInodeNumber;
		const uint8_t fileType = nodeMode == EXT4_S_IFDIR ? 2 : 1;

		if (nodeMode == EXT4_S_IFDIR and crossParent) {
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

		if (nodeMode == EXT4_S_IFDIR and crossParent) {
			if (oldParent.linksCount > 0) {
				--oldParent.linksCount;
			}

			if (!updateDirectoryEntryInode(inode, "..", newParentInodeNumber) or !writeInode(inodeNumber, inode) or
			    !writeInode(oldParentInodeNumber, oldParent) or !writeInode(newParentInodeNumber, newParent)) {
				return false;
			}
		}

		return Utils::flushDevice(device.deviceId);
	}

	auto Ext4Volume::findFirstRootTextFile(uint32_t &inodeNumber, string &name) const -> bool {
		Ext4Inode root {};

		if (!readInode(EXT4_ROOT_INO, root) or (root.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		vector<uint8_t> dirBytes;

		if (!readFile(root, dirBytes, Utils::inodeFileSize(superblock, root))) {
			return false;
		}

		uint64_t offset = 0;

		while (offset + 8 <= dirBytes.size()) {
			const auto *entry = reinterpret_cast<const Ext4DirEntry *>(dirBytes.data() + offset);

			if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
				break;
			}

			if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8) {
				const string candidate(entry->name, entry->nameLen);

				if (candidate != "." and candidate != ".." and candidate.ends_with(".txt")) {
					Ext4Inode candidateInode {};

					if (readInode(entry->inode, candidateInode) and (candidateInode.mode & EXT4_S_IFMT) == EXT4_S_IFREG) {
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

	auto Ext4Volume::readDirectory(const Ext4Inode &dir, vector<VfsDirEntry> &entries, const uint32_t startOffset, bool *hasMore, uint32_t *nextOffset) const -> bool {
		if ((dir.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
			return false;
		}

		vector<uint8_t> dirBytes;

		if (!readFile(dir, dirBytes, Utils::inodeFileSize(superblock, dir))) {
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
			const auto *entry = reinterpret_cast<const Ext4DirEntry *>(dirBytes.data() + offset);

			if (entry->recLen < 8 or offset + entry->recLen > dirBytes.size()) {
				break;
			}

			if (entry->inode != 0 and entry->nameLen > 0 and entry->nameLen <= entry->recLen - 8) {
				const string name(entry->name, entry->nameLen);

				if (name != "." and name != "..") {
					Ext4Inode child {};

					if (!readInode(entry->inode, child)) {
						return false;
					}

					auto &out = entries.emplace_back();

					Utils::fillName(out.name, sizeof(out.name), out.nameLength, name);
					out.nodeType = Utils::inodeNodeType(child);
					out.size = Utils::inodeFileSize(superblock, child);
					out.nodeId = entry->inode;
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

	auto Ext4Volume::lookupPath(const string &path, uint32_t &inodeNumber, Ext4Inode &inode) const -> bool {
		inodeNumber = EXT4_ROOT_INO;

		if (!readInode(inodeNumber, inode)) {
			return false;
		}

		for (const string &part : Utils::splitPath(path)) {
			if (part == "." or part.empty()) {
				continue;
			}

			if (part == ".." or (inode.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
				return false;
			}

			vector<uint8_t> dirBytes;

			if (!readFile(inode, dirBytes, Utils::inodeFileSize(superblock, inode))) {
				return false;
			}

			bool found = false;
			uint64_t offset = 0;

			while (offset + 8 <= dirBytes.size()) {
				const auto *entry = reinterpret_cast<const Ext4DirEntry *>(dirBytes.data() + offset);

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

	void Ext4Volume::testReadFirstTextFile() {
		uint32_t inodeNumber = 0;
		string name;

		if (!findFirstRootTextFile(inodeNumber, name)) {
			printf("Ext4: %s has no root .txt file to test.", device.deviceName);
			fflush(stdout);

			return;
		}

		Ext4Inode inode {};
		vector<uint8_t> bytes;

		if (!readInode(inodeNumber, inode) or !readFile(inode, bytes, 4096)) {
			printf("Ext4: Failed to read %s/%s.", device.deviceName, name.c_str());
			fflush(stdout);

			return;
		}

		const string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());

		printf("Ext4: First text file on %s is %s (%lu bytes): %s", device.deviceName, name.c_str(), Utils::inodeFileSize(superblock, inode), text.c_str());
		fflush(stdout);

		if (!bytes.empty()) {
			const uint8_t firstByte = bytes[0];
			vector<uint8_t> reread;

			if (writeFileOverwrite(inodeNumber, inode, 0, &firstByte, 1) and readInode(inodeNumber, inode) and readFile(inode, reread, bytes.size()) and reread == bytes) {
				printf("Ext4: Verified overwrite write path on %s/%s.", device.deviceName, name.c_str());
				fflush(stdout);
			} else {
				printf("Ext4: Overwrite write path verification failed on %s/%s.", device.deviceName, name.c_str());
				fflush(stdout);
			}
		}
	}

	auto Ext4Volume::getBlockSize() const -> uint64_t {
		return blockSize;
	}

	auto Ext4Volume::getInodeCount() const -> uint32_t {
		return superblock.inodesCount;
	}

	auto Ext4Volume::getBlockCount() const -> uint64_t {
		return Utils::blocksCount(superblock);
	}

	auto Ext4Volume::fileSize(const Ext4Inode &inode) const -> uint64_t {
		return Utils::inodeFileSize(superblock, inode);
	}

	auto Ext4Volume::hadIoFailure() const -> bool {
		return ioFailed;
	}

	auto Ext4Volume::readBlock(const uint64_t fsBlock, uint8_t *buffer) const -> bool {
		if (blockSize == 0 or buffer == nullptr or fsBlock >= Utils::blocksCount(superblock) or
		    fsBlock > UINT64_MAX / blockSize) {
			return false;
		}

		const uint64_t byteOffset = fsBlock * blockSize;
		const uint64_t lba = byteOffset / device.blockSize;
		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!Utils::readDevicePage(device.deviceId, lba, phys, virt)) {
			ioFailed = true;
			return false;
		}

		memcpy(buffer, reinterpret_cast<const void *>(virt), blockSize);
		Utils::freeDevicePage(phys, virt);

		return true;
	}

	auto Ext4Volume::writeBlock(const uint64_t fsBlock, const uint8_t *buffer) const -> bool {
		if (blockSize == 0 or buffer == nullptr or fsBlock >= Utils::blocksCount(superblock) or
		    fsBlock > UINT64_MAX / blockSize) {
			return false;
		}

		const uint64_t byteOffset = fsBlock * blockSize;
		const uint64_t lba = byteOffset / device.blockSize;
		uint64_t phys = 0;
		uint64_t virt = 0;

		if (!Utils::readDevicePage(device.deviceId, lba, phys, virt)) {
			ioFailed = true;
			return false;
		}

		memcpy(reinterpret_cast<void *>(virt), buffer, blockSize);

		const bool success = Utils::writeDevicePage(device.deviceId, lba, phys);

		if (!success) {
			ioFailed = true;
		}

		Utils::freeDevicePage(phys, virt);

		return success;
	}

	auto Ext4Volume::readIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto Ext4Volume::readDoubleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto Ext4Volume::readTripleIndirectPointer(const uint32_t block, const uint64_t index, uint64_t &fsBlock) const -> bool {
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

	auto Ext4Volume::resolveExtentNode(const uint8_t *node, const uint64_t fileBlock, uint64_t &fsBlock) const -> bool {
		const auto *header = reinterpret_cast<const Ext4ExtentHeader *>(node);

		if (header->magic != EXT4_EXT_MAGIC || header->entries > header->max) {
			return false;
		}

		if (header->depth == 0) {
			const auto *extents = reinterpret_cast<const Ext4Extent *>(node + sizeof(Ext4ExtentHeader));

			for (uint16_t i = 0; i < header->entries; ++i) {
				const Ext4Extent &extent = extents[i];
				const uint32_t len = Utils::extentLength(extent);

				if (len == 0) {
					continue;
				}

				if (fileBlock >= extent.block && fileBlock < static_cast<uint64_t>(extent.block) + len) {
					fsBlock = Utils::extentStartBlock(extent) + (fileBlock - extent.block);

					return fsBlock != 0;
				}
			}

			return false;
		}

		const auto *indices = reinterpret_cast<const Ext4ExtentIndex *>(node + sizeof(Ext4ExtentHeader));
		const Ext4ExtentIndex *selected = nullptr;

		for (uint16_t i = 0; i < header->entries; ++i) {
			if (fileBlock < indices[i].block) {
				break;
			}

			selected = &indices[i];
		}

		if (selected == nullptr) {
			return false;
		}

		vector<uint8_t> child;
		child.resize(blockSize);

		if (!readBlock(Utils::extentIndexLeafBlock(*selected), child.data())) {
			return false;
		}

		return resolveExtentNode(child.data(), fileBlock, fsBlock);
	}

	auto Ext4Volume::resolveExtentBlock(const Ext4Inode &inode, const uint64_t fileBlock, uint64_t &fsBlock) const -> bool {
		return resolveExtentNode(reinterpret_cast<const uint8_t *>(inode.block), fileBlock, fsBlock);
	}
}
