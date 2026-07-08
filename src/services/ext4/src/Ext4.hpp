#ifndef HORIZONOS_EXT4_HPP
#define HORIZONOS_EXT4_HPP

#include "StorageProtocol.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

namespace horizonos::services::ext4 {
	constexpr uint16_t EXT4_SUPER_MAGIC = 0xEF53;
	constexpr uint64_t EXT4_SUPERBLOCK_OFFSET = 1024;
	constexpr uint32_t EXT4_GOOD_OLD_REV = 0;
	constexpr uint32_t EXT4_FEATURE_INCOMPAT_FILETYPE = 0x0002;
	constexpr uint32_t EXT4_FEATURE_INCOMPAT_EXTENTS = 0x0040;
	constexpr uint32_t EXT4_FEATURE_INCOMPAT_64BIT = 0x0080;
	constexpr uint32_t EXT4_FEATURE_INCOMPAT_FLEX_BG = 0x0200;
	constexpr uint32_t EXT4_SUPPORTED_INCOMPAT_FEATURES = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT | EXT4_FEATURE_INCOMPAT_FLEX_BG;
	constexpr uint32_t EXT4_FEATURE_RO_COMPAT_LARGE_FILE = 0x0002;
	constexpr uint32_t EXT4_FEATURE_RO_COMPAT_HUGE_FILE = 0x0008;
	constexpr uint32_t EXT4_FEATURE_RO_COMPAT_DIR_NLINK = 0x0020;
	constexpr uint32_t EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE = 0x0040;
	constexpr uint32_t EXT4_FEATURE_RO_COMPAT_METADATA_CSUM = 0x0400;
	constexpr uint32_t EXT4_UNSUPPORTED_WRITE_INCOMPAT_FEATURES = EXT4_FEATURE_INCOMPAT_64BIT | EXT4_FEATURE_INCOMPAT_FLEX_BG;
	constexpr uint32_t EXT4_UNSUPPORTED_WRITE_RO_COMPAT_FEATURES = EXT4_FEATURE_RO_COMPAT_HUGE_FILE | EXT4_FEATURE_RO_COMPAT_DIR_NLINK | EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE | EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	constexpr uint16_t EXT4_GROUP_DESCRIPTOR_SIZE = 32;
	constexpr uint16_t EXT4_EXT_MAGIC = 0xF30A;
	constexpr uint32_t EXT4_EXTENTS_FL = 0x00080000;
	constexpr uint16_t EXT4_S_IFMT = 0xF000;
	constexpr uint16_t EXT4_S_IFREG = 0x8000;
	constexpr uint16_t EXT4_S_IFDIR = 0x4000;
	constexpr uint16_t EXT4_S_IFLNK = 0xA000;
	constexpr uint32_t EXT4_ROOT_INO = 2;

	struct Ext4Superblock {
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

	struct Ext4GroupDescriptor {
		uint32_t blockBitmapLo {};
		uint32_t inodeBitmapLo {};
		uint32_t inodeTableLo {};
		uint16_t freeBlocksCountLo {};
		uint16_t freeInodesCountLo {};
		uint16_t usedDirsCountLo {};
		uint16_t flags {};
		uint32_t reserved[3] {};
	} __attribute__((packed));

	struct Ext4Inode {
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

	struct Ext4DirEntry {
		uint32_t inode {};
		uint16_t recLen {};
		uint8_t nameLen {};
		uint8_t fileType {};
		char name[];
	} __attribute__((packed));

	struct Ext4ExtentHeader {
		uint16_t magic {};
		uint16_t entries {};
		uint16_t max {};
		uint16_t depth {};
		uint32_t generation {};
	} __attribute__((packed));

	struct Ext4ExtentIndex {
		uint32_t block {};
		uint32_t leafLo {};
		uint16_t leafHi {};
		uint16_t unused {};
	} __attribute__((packed));

	struct Ext4Extent {
		uint32_t block {};
		uint16_t len {};
		uint16_t startHi {};
		uint32_t startLo {};
	} __attribute__((packed));

	class Ext4Volume {
	public:
		explicit Ext4Volume(const StorageFsProbeDeviceMsgData &device);

		auto load() -> bool;
		auto readInode(uint32_t inodeNumber, Ext4Inode &out) const -> bool;
		auto writeInode(uint32_t inodeNumber, const Ext4Inode &inode) const -> bool;
		auto resolveDataBlock(const Ext4Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) const -> bool;
		static auto inodeUsesExtents(const Ext4Inode &inode) -> bool;
		auto readFile(const Ext4Inode &inode, vector<uint8_t> &out, uint64_t maxBytes = 65536) const -> bool;
		auto readFileRange(const Ext4Inode &inode, uint64_t offset, uint32_t length, vector<uint8_t> &out) const -> bool;
		auto writeFileOverwrite(uint32_t inodeNumber, const Ext4Inode &inode, uint64_t offset, const uint8_t *data, uint64_t length) const -> bool;
		auto writeFile(uint32_t inodeNumber, Ext4Inode &inode, uint64_t offset, const uint8_t *data, uint64_t length) -> bool;
		auto createFile(const string &path) -> bool;
		auto createDirectory(const string &path) -> bool;
		auto createHardLink(const string &oldPath, const string &newPath) -> bool;
		auto createSymlink(const string &target, const string &linkPath) -> bool;
		auto readSymlink(const string &linkPath, string &target) const -> bool;
		auto unlinkFile(const string &path) -> bool;
		auto renameFile(const string &oldPath, const string &newPath) -> bool;
		auto truncateFile(const string &path, uint64_t size) -> bool;
		auto findFirstRootTextFile(uint32_t &inodeNumber, string &name) const -> bool;
		auto lookupPath(const string &path, uint32_t &inodeNumber, Ext4Inode &inode) const -> bool;
		auto readDirectory(const Ext4Inode &dir, vector<VfsDirEntry> &entries, uint32_t startOffset = 0, bool *hasMore = nullptr, uint32_t *nextOffset = nullptr) const -> bool;
		void testReadFirstTextFile();

		auto getBlockSize() const -> uint64_t;
		auto getInodeCount() const -> uint32_t;
		auto getBlockCount() const -> uint64_t;
		auto fileSize(const Ext4Inode &inode) const -> uint64_t;

	private:
		auto readBlock(uint64_t fsBlock, uint8_t *buffer) const -> bool;
		auto writeBlock(uint64_t fsBlock, const uint8_t *buffer) const -> bool;
		auto readIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;
		auto readDoubleIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;
		auto readTripleIndirectPointer(uint32_t block, uint64_t index, uint64_t &fsBlock) const -> bool;
		auto resolveExtentBlock(const Ext4Inode &inode, uint64_t fileBlock, uint64_t &fsBlock) const -> bool;
		auto resolveExtentNode(const uint8_t *node, uint64_t fileBlock, uint64_t &fsBlock) const -> bool;
		auto ensureExtentDataBlock(Ext4Inode &inode, uint64_t fileBlock, uint32_t &fsBlock) -> bool;
		static auto appendExtentToLeaf(uint8_t *leaf, uint64_t fileBlock, uint32_t allocatedBlock) -> bool;
		auto truncateExtentLeaf(uint8_t *leaf, uint64_t keepBlocks, Ext4Inode &inode) -> bool;
		auto truncateExtentBlocks(Ext4Inode &inode, uint64_t keepBlocks) -> bool;
		auto mutationsSupported() const -> bool;
		auto writeSuperblock() const -> bool;
		auto writeGroupDescriptor(uint32_t group) const -> bool;
		auto allocateBlock(uint32_t &block) -> bool;
		auto allocateInode(uint32_t &inodeNumber) -> bool;
		auto freeBlock(uint32_t block) -> bool;
		auto freeInode(uint32_t inodeNumber) -> bool;
		auto ensureDataBlock(Ext4Inode &inode, uint64_t fileBlock, uint32_t &fsBlock) -> bool;
		auto ensureIndirectDataBlock(Ext4Inode &inode, uint32_t &pointerBlock, uint32_t depth, uint64_t index, uint32_t &fsBlock) -> bool;
		auto addDirectoryEntry(uint32_t parentInodeNumber, Ext4Inode &parent, uint32_t childInodeNumber, const string &name, uint8_t fileType) -> bool;
		auto removeDirectoryEntry(uint32_t parentInodeNumber, Ext4Inode &parent, const string &name, uint32_t &removedInode) -> bool;
		auto directoryIsEmpty(const Ext4Inode &dir) const -> bool;
		auto updateDirectoryEntryInode(const Ext4Inode &dir, const string &name, uint32_t inodeNumber) const -> bool;
		auto freeInodeBlocks(Ext4Inode &inode, uint64_t keepBlocks) -> bool;
		auto freeIndirectBlocks(uint32_t &pointerBlock, uint32_t depth, uint64_t keepBlocks, uint64_t span) -> bool;
		auto countIndirectBlocks(uint32_t pointerBlock, uint32_t depth) const -> uint64_t;
		auto countInodeBlocks(const Ext4Inode &inode) const -> uint64_t;
		static auto splitParentPath(const string &path, string &parentPath, string &name) -> bool;
		static void setInodeFileSize(Ext4Inode &inode, uint64_t size) ;

		StorageFsProbeDeviceMsgData device {};
		Ext4Superblock superblock {};
		uint64_t blockSize {};
		vector<Ext4GroupDescriptor> groupDescriptors {};
	};
}

#endif
