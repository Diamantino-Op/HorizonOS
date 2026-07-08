#ifndef HORIZONOS_EXT4_SERVICE_HPP
#define HORIZONOS_EXT4_SERVICE_HPP

#include "Ext4.hpp"
#include "StorageProtocol.hpp"

#include <cstdint>
#include <pthread.h>
#include <vector>

using namespace std;

namespace horizonos::services::ext4 {
	struct MountedExt4 {
		uint64_t mountId {};
		StorageFsProbeDeviceMsgData device {};
	};

	struct ScopedMutex {
		pthread_mutex_t *mutex {};

		explicit ScopedMutex(pthread_mutex_t &lock) : mutex(&lock) {
			pthread_mutex_lock(mutex);
		}

		~ScopedMutex() {
			pthread_mutex_unlock(mutex);
		}

		ScopedMutex(const ScopedMutex &) = delete;
		auto operator=(const ScopedMutex &) -> ScopedMutex & = delete;
	};

	class Utils {
	public:
		static void fillName(char *dst, size_t dstSize, size_t &length, const string &name);
		static auto validName(const char *name, size_t length, size_t maxLength, string &out) -> bool;
		static auto registerWithNameRegistry(const char *name) -> bool;
		static auto waitForStorage() -> uint64_t;
		static auto waitForServicePort(const char *name) -> uint64_t;
		static auto registerWithVfs(const char *fsName) -> bool;
		static auto registerFsHandler() -> bool;
		static auto readDevicePage(uint64_t deviceId, uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool;
		static auto writeDevicePage(uint64_t deviceId, uint64_t lba, uint64_t phys) -> bool;
		static auto flushDevice(uint64_t deviceId) -> bool;
		static void freeDevicePage(uint64_t phys, uint64_t virt);
		static auto inodeSize(const Ext4Superblock &superblock) -> uint16_t;
		static auto blocksCount(const Ext4Superblock &superblock) -> uint64_t;
		static auto inodeFileSize(const Ext4Superblock &superblock, const Ext4Inode &inode) -> uint64_t;
		static auto extentStartBlock(const Ext4Extent &extent) -> uint64_t;
		static auto extentIndexLeafBlock(const Ext4ExtentIndex &index) -> uint64_t;
		static auto extentLength(const Ext4Extent &extent) -> uint32_t;
		static auto groupDescriptorInodeTable(const Ext4Superblock &superblock, const Ext4GroupDescriptor &desc) -> uint64_t;
		static auto inodeNodeType(const Ext4Inode &inode) -> uint8_t;
		static auto ext4DirectoryFileType(const Ext4Inode &inode) -> uint8_t;
		static auto splitPath(const string &path) -> vector<string>;
		static auto ext4DirRecLen(size_t nameLength) -> uint16_t;
		static auto bitmapBitSet(const vector<uint8_t> &bitmap, uint32_t bit) -> bool;
		static void bitmapSetBit(vector<uint8_t> &bitmap, uint32_t bit);
		static void bitmapClearBit(vector<uint8_t> &bitmap, uint32_t bit);
		static auto probeExt4(const StorageFsProbeDeviceMsgData &device) -> bool;
		static auto validPath(const char *path, size_t length, string &out) -> bool;
	};

	class Ext4Service {
	public:
		auto start() -> int;

		auto allocateStorageRequestId() -> uint64_t {
			const ScopedMutex lock(storageRequestIdLock);
			return nextStorageRequestId++;
		}

		auto mountedDevice(uint64_t mountId, StorageFsProbeDeviceMsgData &device) -> bool {
			const ScopedMutex lock(mountsLock);

			for (const auto &mount : mounts) {
				if (mount.mountId == mountId) {
					device = mount.device;

					return true;
				}
			}

			return false;
		}

		uint64_t ext4Port = 0;
		uint64_t storagePort = 0;
		uint64_t storageReplyPort = 0;
		uint64_t nextStorageRequestId = 1;
		uint64_t nextMountId = 1;
		pthread_mutex_t storageRpcLock = PTHREAD_MUTEX_INITIALIZER;
		pthread_mutex_t storageRequestIdLock = PTHREAD_MUTEX_INITIALIZER;
		pthread_mutex_t mountsLock = PTHREAD_MUTEX_INITIALIZER;
		vector<MountedExt4> mounts;
	};

	extern Ext4Service service;
}

#endif
