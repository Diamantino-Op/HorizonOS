#ifndef HORIZONOS_EXT2_HPP
#define HORIZONOS_EXT2_HPP

#include "StorageProtocol.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

namespace {
	constexpr uint16_t EXT2_SUPER_MAGIC = 0xEF53;
	constexpr uint64_t EXT2_SUPERBLOCK_OFFSET = 1024;
	constexpr uint32_t EXT2_GOOD_OLD_REV = 0;
	constexpr uint32_t EXT2_FEATURE_INCOMPAT_FILETYPE = 0x0002;
	constexpr uint32_t EXT2_SUPPORTED_INCOMPAT_FEATURES = EXT2_FEATURE_INCOMPAT_FILETYPE;
	constexpr uint32_t EXT2_FEATURE_RO_COMPAT_LARGE_FILE = 0x0002;
	constexpr uint16_t EXT2_GROUP_DESCRIPTOR_SIZE = 32;
	constexpr uint16_t EXT2_S_IFMT = 0xF000;
	constexpr uint16_t EXT2_S_IFREG = 0x8000;
	constexpr uint16_t EXT2_S_IFDIR = 0x4000;
	constexpr uint32_t EXT2_ROOT_INO = 2;

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
		uint16_t state {};
		uint16_t errors {};
		uint16_t minorRevisionLevel {};
		uint32_t lastCheck {};
		uint32_t checkInterval {};
		uint32_t creatorOs {};
		uint32_t revisionLevel {};
		uint16_t defaultReservedUid {};
		uint16_t defaultReservedGid {};
		uint32_t firstInode {};
		uint16_t inodeSize {};
		uint16_t blockGroupNumber {};
		uint32_t featureCompat {};
		uint32_t featureIncompat {};
		uint32_t featureRoCompat {};
	} __attribute__((packed));

	struct Ext2GroupDescriptor {
		uint32_t blockBitmapLo {};
		uint32_t inodeBitmapLo {};
		uint32_t inodeTableLo {};
		uint16_t freeBlocksCountLo {};
		uint16_t freeInodesCountLo {};
		uint16_t usedDirsCountLo {};
		uint16_t flags {};
		uint32_t reserved[3] {};
	} __attribute__((packed));

	struct Ext2Inode {
		uint16_t mode {};
		uint16_t uid {};
		uint32_t sizeLo {};
		uint32_t atime {};
		uint32_t ctime {};
		uint32_t mtime {};
		uint32_t dtime {};
		uint16_t gid {};
		uint16_t linksCount {};
		uint32_t blocks {};
		uint32_t flags {};
		uint32_t osd1 {};
		uint32_t block[15] {};
		uint32_t generation {};
		uint32_t fileAcl {};
		uint32_t sizeHighOrDirAcl {};
		uint32_t faddr {};
		uint8_t osd2[12] {};
	} __attribute__((packed));

	struct Ext2DirEntry {
		uint32_t inode {};
		uint16_t recLen {};
		uint8_t nameLen {};
		uint8_t fileType {};
		char name[];
	} __attribute__((packed));

	class Ext2Volume {
	public:
		explicit Ext2Volume(const StorageFsProbeDeviceMsgData &device);

		auto load() -> bool;
		auto readInode(uint32_t inodeNumber, Ext2Inode &out) -> bool;
		auto writeInode(uint32_t inodeNumber, const Ext2Inode &inode) -> bool;
		auto resolveDataBlock(const Ext2Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) -> bool;
		auto readFile(const Ext2Inode &inode, vector<uint8_t> &out, uint64_t maxBytes = 65536) -> bool;
		auto writeFileOverwrite(uint32_t inodeNumber, Ext2Inode &inode, uint64_t offset, const uint8_t *data, uint64_t length) -> bool;
		auto findFirstRootTextFile(uint32_t &inodeNumber, string &name) -> bool;
		void testReadFirstTextFile();

		auto getBlockSize() const -> uint64_t;
		auto getInodeCount() const -> uint32_t;
		auto getBlockCount() const -> uint64_t;

	private:
		auto readBlock(uint64_t fsBlock, uint8_t *buffer) const -> bool;
		auto writeBlock(uint64_t fsBlock, const uint8_t *buffer) const -> bool;
		auto readIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;
		auto readDoubleIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;
		auto readTripleIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;

		StorageFsProbeDeviceMsgData device {};
		Ext2Superblock superblock {};
		uint64_t blockSize {};
		vector<Ext2GroupDescriptor> groupDescriptors {};
	};
}

#endif
