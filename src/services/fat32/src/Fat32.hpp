#ifndef HORIZONOS_FAT32_HPP
#define HORIZONOS_FAT32_HPP

#include "../../ext4/src/StorageProtocol.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

constexpr uint16_t FAT_BOOT_SIGNATURE = 0xAA55;
constexpr uint32_t FAT32_MIN_CLUSTERS = 65525;
constexpr uint32_t FAT32_EOC = 0x0FFFFFF8;
constexpr uint32_t FAT32_BAD_CLUSTER = 0x0FFFFFF7;
constexpr uint32_t FAT32_CLUSTER_MASK = 0x0FFFFFFF;
constexpr uint8_t FAT_ATTR_VOLUME_ID = 0x08;
constexpr uint8_t FAT_ATTR_DIRECTORY = 0x10;
constexpr uint8_t FAT_ATTR_LFN = 0x0F;

struct MountedFat32 {
	uint64_t mountId {};
	StorageFsProbeDeviceMsgData device {};
};

struct FatBootSector {
	uint8_t jump[3] {};
	char oemName[8] {};
	uint16_t bytesPerSector {};
	uint8_t sectorsPerCluster {};
	uint16_t reservedSectors {};
	uint8_t fatCount {};
	uint16_t rootEntryCount {};
	uint16_t totalSectors16 {};
	uint8_t media {};
	uint16_t fatSize16 {};
	uint16_t sectorsPerTrack {};
	uint16_t headCount {};
	uint32_t hiddenSectors {};
	uint32_t totalSectors32 {};
	uint32_t fatSize32 {};
	uint16_t extFlags {};
	uint16_t fsVersion {};
	uint32_t rootCluster {};
	uint16_t fsInfoSector {};
	uint16_t backupBootSector {};
	uint8_t reserved[12] {};
	uint8_t driveNumber {};
	uint8_t reserved1 {};
	uint8_t bootSignature {};
	uint32_t volumeId {};
	char volumeLabel[11] {};
	char fsType[8] {};
	uint8_t bootCode[420] {};
	uint16_t signature {};
} __attribute__((packed));

struct FatDirEntryRaw {
	uint8_t name[11] {};
	uint8_t attr {};
	uint8_t ntReserved {};
	uint8_t creationTenths {};
	uint16_t creationTime {};
	uint16_t creationDate {};
	uint16_t accessDate {};
	uint16_t firstClusterHigh {};
	uint16_t writeTime {};
	uint16_t writeDate {};
	uint16_t firstClusterLow {};
	uint32_t fileSize {};
} __attribute__((packed));

struct FatDirEntry {
	string name;
	uint8_t attr {};
	uint32_t firstCluster {};
	uint32_t size {};
	uint32_t dirCluster {};
	uint32_t dirOffset {};
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

class Fat32Service {
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

	uint64_t fat32Port = 0;
	uint64_t storagePort = 0;
	uint64_t storageReplyPort = 0;
	uint64_t nextStorageRequestId = 1;
	uint64_t nextMountId = 1;
	pthread_mutex_t storageRpcLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t volumeLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t storageRequestIdLock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t mountsLock = PTHREAD_MUTEX_INITIALIZER;
	vector<MountedFat32> mounts;
};

extern Fat32Service service;

class Fat32Utils {
public:
	static auto allocateStorageRequestId() -> uint64_t;
	static void fillName(char *dst, size_t dstSize, size_t &length, const string &name);
	static auto validName(const char *name, size_t length, size_t maxLength, string &out) -> bool;
	static auto validPath(const char *path, size_t length, string &out) -> bool;
	static auto isPowerOfTwo(uint32_t value) -> bool;
	static auto upperAscii(string value) -> string;
	static auto pathParts(const string &path) -> vector<string>;
	static auto registerWithNameRegistry(const char *name) -> bool;
	static auto waitForStorage() -> uint64_t;
	static auto waitForServicePort(const char *name) -> uint64_t;
	static auto registerWithVfs(const char *fsName) -> bool;
	static auto registerFsHandler() -> bool;
	static auto readDevicePage(uint64_t deviceId, uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool;
	static void freeDevicePage(uint64_t &phys, uint64_t &virt);
	static auto writeDevicePage(uint64_t deviceId, uint64_t lba, uint64_t phys) -> bool;
	static auto flushDevice(uint64_t deviceId) -> bool;
	static auto probeFat32(const StorageFsProbeDeviceMsgData &device) -> bool;
};

namespace {
	class Fat32Volume {
	public:
		explicit Fat32Volume(const StorageFsProbeDeviceMsgData &dev) : device(dev) {
			pthread_mutex_lock(&service.volumeLock);
		}

		~Fat32Volume() {
			pthread_mutex_unlock(&service.volumeLock);
		}

		Fat32Volume(const Fat32Volume &) = delete;
		auto operator=(const Fat32Volume &) -> Fat32Volume & = delete;

		auto load() -> bool {
			if (device.blockSize == 0 or device.blockSize > 0x1000 or (0x1000 % device.blockSize) != 0) {
				return false;
			}

			vector<uint8_t> sector;

			if (!readBytes(0, sizeof(FatBootSector), sector)) {
				return false;
			}

			memcpy(&boot, sector.data(), sizeof(boot));

			if (boot.signature != FAT_BOOT_SIGNATURE or boot.bytesPerSector != device.blockSize) {
				return false;
			}

			if (!Fat32Utils::isPowerOfTwo(boot.bytesPerSector) or boot.bytesPerSector < 512 or boot.bytesPerSector > 4096) {
				return false;
			}

			if (!Fat32Utils::isPowerOfTwo(boot.sectorsPerCluster) or boot.reservedSectors == 0 or boot.fatCount == 0 or boot.fatSize32 == 0) {
				return false;
			}

			if (boot.rootEntryCount != 0 or boot.fatSize16 != 0 or boot.rootCluster < 2) {
				return false;
			}

			totalSectors = boot.totalSectors16 != 0 ? boot.totalSectors16 : boot.totalSectors32;
			firstFatSector = boot.reservedSectors;
			firstDataSector = boot.reservedSectors + static_cast<uint64_t>(boot.fatCount) * boot.fatSize32;

			if (totalSectors <= firstDataSector) {
				return false;
			}

			const uint64_t dataSectors = totalSectors - firstDataSector;
			clusterCount = dataSectors / boot.sectorsPerCluster;

			return clusterCount >= FAT32_MIN_CLUSTERS;
		}

		auto lookupPath(const string &path, FatDirEntry &entry) const -> bool {
			if (path.empty() or path == "/") {
				entry.name = "/";
				entry.attr = FAT_ATTR_DIRECTORY;
				entry.firstCluster = boot.rootCluster;
				entry.size = 0;

				return true;
			}

			FatDirEntry current {};
			current.name = "/";
			current.attr = FAT_ATTR_DIRECTORY;
			current.firstCluster = boot.rootCluster;

			for (const string &part : Fat32Utils::pathParts(path)) {
				if ((current.attr & FAT_ATTR_DIRECTORY) == 0) {
					return false;
				}

				vector<FatDirEntry> entries;

				if (!readDirectoryEntries(current.firstCluster, entries)) {
					return false;
				}

				const string wanted = Fat32Utils::upperAscii(part);
				bool found = false;

				for (const auto &candidate : entries) {
					if (Fat32Utils::upperAscii(candidate.name) == wanted) {
						current = candidate;
						found = true;
						break;
					}
				}

				if (!found) {
					return false;
				}
			}

			entry = current;

			return true;
		}

		auto readDirectory(const FatDirEntry &dir, vector<VfsDirEntry> &out, const uint32_t startOffset, bool *hasMore, uint32_t *nextOffset) const -> bool {
			if ((dir.attr & FAT_ATTR_DIRECTORY) == 0) {
				return false;
			}

			vector<uint8_t> bytes;

			if (!readClusterChain(dir.firstCluster, bytes, 0)) {
				return false;
			}

			uint32_t offset = startOffset - (startOffset % sizeof(FatDirEntryRaw));
			string longName;

			while (offset + sizeof(FatDirEntryRaw) <= bytes.size()) {
				const auto *raw = reinterpret_cast<const FatDirEntryRaw *>(bytes.data() + offset);
				const uint8_t first = raw->name[0];

				if (first == 0x00) {
					break;
				}

				if (first == 0xE5) {
					longName.clear();
					offset += sizeof(FatDirEntryRaw);
					continue;
				}

				if (raw->attr == FAT_ATTR_LFN) {
					longName = readLongNamePart(bytes.data() + offset) + longName;
					offset += sizeof(FatDirEntryRaw);
					continue;
				}

				if ((raw->attr & FAT_ATTR_VOLUME_ID) != 0) {
					longName.clear();
					offset += sizeof(FatDirEntryRaw);
					continue;
				}

				FatDirEntry entry = makeEntry(*raw, longName, dir.firstCluster, offset);
				longName.clear();

				if (entry.name != "." and entry.name != "..") {
					auto vfsEntry = VfsDirEntry();
					Fat32Utils::fillName(vfsEntry.name, sizeof(vfsEntry.name), vfsEntry.nameLength, entry.name);
					vfsEntry.nodeType = (entry.attr & FAT_ATTR_DIRECTORY) != 0 ? VFS_NODE_DIRECTORY : VFS_NODE_FILE;
					vfsEntry.size = entry.size;
					vfsEntry.nodeId = entry.firstCluster;
					out.push_back(vfsEntry);

					if (out.size() == VFS_MAX_DIR_ENTRIES) {
						offset += sizeof(FatDirEntryRaw);
						if (hasMore != nullptr) {
							*hasMore = offset < bytes.size();
						}
						if (nextOffset != nullptr) {
							*nextOffset = offset;
						}
						return true;
					}
				}

				offset += sizeof(FatDirEntryRaw);
			}

			if (hasMore != nullptr) {
				*hasMore = false;
			}
			if (nextOffset != nullptr) {
				*nextOffset = offset;
			}

			return true;
		}

		auto readFileRange(const FatDirEntry &entry, const uint64_t offset, const uint32_t length, vector<uint8_t> &out) const -> bool {
			if ((entry.attr & FAT_ATTR_DIRECTORY) != 0) {
				return false;
			}

			if (offset >= entry.size or length == 0) {
				out.clear();
				return true;
			}

			const uint32_t bytesToRead = min<uint64_t>(length, static_cast<uint64_t>(entry.size) - offset);
			vector<uint8_t> bytes;

			if (!readClusterChain(entry.firstCluster, bytes, entry.size)) {
				return false;
			}

			if (offset > bytes.size()) {
				return false;
			}

			const uint32_t available = min<uint64_t>(bytesToRead, bytes.size() - offset);
			out.resize(available);
			memcpy(out.data(), bytes.data() + offset, available);

			return true;
		}

		auto writeFile(const string &path, const uint64_t offset, const uint8_t *data, const uint32_t length, uint64_t &newSize) -> bool {
			auto fail = [&](const char *stage) {
				printf("FAT32: Write %s failed on %s path='%s' offset=%lu length=%u.", stage, device.deviceName, path.c_str(), offset, length);
				fflush(stdout);

				return false;
			};

			if (length > 0 and data == nullptr) {
				return fail("validation");
			}

			FatDirEntry entry {};

			if (!lookupPath(path, entry) or (entry.attr & FAT_ATTR_DIRECTORY) != 0) {
				return fail("lookup");
			}

			if (length == 0) {
				newSize = entry.size;
				return true;
			}

			if (offset > 0xffffffffULL or offset + length > 0xffffffffULL) {
				return fail("range validation");
			}

			const uint32_t targetSize = static_cast<uint32_t>(max<uint64_t>(entry.size, offset + length));

			if (!ensureFileClusters(entry, targetSize)) {
				return fail("cluster allocation");
			}

			uint32_t cluster = entry.firstCluster;
			const uint32_t clusterBytes = bytesPerCluster();
			uint64_t skipClusters = offset / clusterBytes;

			while (skipClusters-- > 0) {
				uint32_t next = 0;

				if (!readFatEntry(cluster, next)) {
					return fail("cluster-chain lookup");
				}

				cluster = next;
			}

			uint64_t written = 0;
			uint32_t clusterOffset = offset % clusterBytes;

			while (written < length) {
				vector<uint8_t> clusterData;

				if (!readCluster(cluster, clusterData)) {
					return fail("data read");
				}

				const uint32_t toCopy = min<uint64_t>(clusterBytes - clusterOffset, length - written);
				memcpy(clusterData.data() + clusterOffset, data + written, toCopy);

				if (!writeCluster(cluster, clusterData.data())) {
					return fail("data write");
				}

				written += toCopy;
				clusterOffset = 0;

				if (written < length) {
					uint32_t next = 0;

					if (!readFatEntry(cluster, next)) {
						return fail("cluster-chain advance");
					}

					cluster = next;
				}
			}

			entry.size = targetSize;

			if (!updateDirectoryEntry(entry)) {
				return fail("directory update");
			}

			newSize = entry.size;

			if (!Fat32Utils::flushDevice(device.deviceId)) {
				return fail("flush");
			}

			return true;
		}

		auto createFile(const string &path, uint64_t &nodeId) -> bool {
			string parentPath;
			string name;

			if (!splitParentPath(path, parentPath, name)) {
				return false;
			}

			FatDirEntry existing {};

			if (lookupPath(path, existing)) {
				return false;
			}

			FatDirEntry parent {};

			if (!lookupPath(parentPath, parent) or (parent.attr & FAT_ATTR_DIRECTORY) == 0) {
				return false;
			}

			vector<FatDirEntryRaw> rawEntries;

			if (!makeDirectoryEntries(name, 0x20, 0, 0, rawEntries)) {
				return false;
			}

			FatDirEntry created {};

			if (!addDirectoryEntries(parent, rawEntries, created)) {
				return false;
			}

			nodeId = this->nodeId(created);

			return Fat32Utils::flushDevice(device.deviceId);
		}

		auto createDirectory(const string &path, uint64_t &nodeId) -> bool {
			string parentPath;
			string name;

			if (!splitParentPath(path, parentPath, name)) {
				return false;
			}

			FatDirEntry existing {};

			if (lookupPath(path, existing)) {
				return false;
			}

			FatDirEntry parent {};

			if (!lookupPath(parentPath, parent) or (parent.attr & FAT_ATTR_DIRECTORY) == 0) {
				return false;
			}

			uint32_t cluster = 0;

			if (!allocateCluster(cluster)) {
				return false;
			}

			vector<uint8_t> dirBytes;

			dirBytes.resize(bytesPerCluster());

			auto *dot = reinterpret_cast<FatDirEntryRaw *>(dirBytes.data());
			memset(dot->name, ' ', sizeof(dot->name));
			dot->name[0] = '.';
			dot->attr = FAT_ATTR_DIRECTORY;
			dot->firstClusterHigh = static_cast<uint16_t>(cluster >> 16);
			dot->firstClusterLow = static_cast<uint16_t>(cluster);

			auto *dotDot = reinterpret_cast<FatDirEntryRaw *>(dirBytes.data() + sizeof(FatDirEntryRaw));
			memset(dotDot->name, ' ', sizeof(dotDot->name));
			dotDot->name[0] = '.';
			dotDot->name[1] = '.';
			dotDot->attr = FAT_ATTR_DIRECTORY;
			dotDot->firstClusterHigh = static_cast<uint16_t>(parent.firstCluster >> 16);
			dotDot->firstClusterLow = static_cast<uint16_t>(parent.firstCluster);

			if (!writeCluster(cluster, dirBytes.data())) {
				return false;
			}

			vector<FatDirEntryRaw> rawEntries;

			if (!makeDirectoryEntries(name, FAT_ATTR_DIRECTORY, cluster, 0, rawEntries)) {
				return false;
			}

			FatDirEntry created {};

			if (!addDirectoryEntries(parent, rawEntries, created)) {
				return false;
			}

			nodeId = this->nodeId(created);

			return Fat32Utils::flushDevice(device.deviceId);
		}

		auto truncateFile(const string &path, const uint64_t size, uint64_t &newSize) -> bool {
			if (size > 0xffffffffULL) {
				return false;
			}

			FatDirEntry entry {};

			if (!lookupPath(path, entry) or (entry.attr & FAT_ATTR_DIRECTORY) != 0) {
				return false;
			}

			if (!resizeFile(entry, static_cast<uint32_t>(size))) {
				return false;
			}

			newSize = entry.size;

			return Fat32Utils::flushDevice(device.deviceId);
		}

		auto unlinkFile(const string &path) -> bool {
			FatDirEntry entry {};

			if (!lookupPath(path, entry) or entry.name == "/") {
				return false;
			}

			if ((entry.attr & FAT_ATTR_DIRECTORY) != 0 and !directoryIsEmpty(entry)) {
				return false;
			}

			if (!freeClusterChain(entry.firstCluster)) {
				return false;
			}

			if (!deleteDirectoryEntry(entry)) {
				return false;
			}

			return Fat32Utils::flushDevice(device.deviceId);
		}

		auto renameFile(const string &oldPath, const string &newPath) -> bool {
			if (oldPath == newPath) {
				return true;
			}

			string parentPath;
			string name;

			if (!splitParentPath(newPath, parentPath, name)) {
				return false;
			}

			FatDirEntry existing {};

			if (lookupPath(newPath, existing)) {
				return false;
			}

			FatDirEntry oldEntry {};

			if (!lookupPath(oldPath, oldEntry) or oldEntry.name == "/") {
				return false;
			}

			FatDirEntry parent {};

			if (!lookupPath(parentPath, parent) or (parent.attr & FAT_ATTR_DIRECTORY) == 0) {
				return false;
			}

			vector<FatDirEntryRaw> rawEntries;

			if (!makeDirectoryEntries(name, oldEntry.attr, oldEntry.firstCluster, oldEntry.size, rawEntries)) {
				return false;
			}

			FatDirEntry created {};

			if (!addDirectoryEntries(parent, rawEntries, created)) {
				return false;
			}

			if (!deleteDirectoryEntry(oldEntry)) {
				return false;
			}

			return Fat32Utils::flushDevice(device.deviceId);
		}

		auto nodeId(const FatDirEntry &entry) const -> uint64_t {
			if (entry.name == "/") {
				return boot.rootCluster;
			}

			return entry.firstCluster != 0 ? entry.firstCluster :
				(0x8000000000000000ULL | (static_cast<uint64_t>(entry.dirCluster) << 32) | entry.dirOffset);
		}

		auto nodeType(const FatDirEntry &entry) const -> uint8_t {
			return (entry.attr & FAT_ATTR_DIRECTORY) != 0 ? VFS_NODE_DIRECTORY : VFS_NODE_FILE;
		}

		auto size(const FatDirEntry &entry) const -> uint64_t {
			return (entry.attr & FAT_ATTR_DIRECTORY) != 0 ? 0 : entry.size;
		}

		auto getClusterCount() const -> uint64_t {
			return clusterCount;
		}

	private:
		auto readBytes(const uint64_t byteOffset, const size_t length, vector<uint8_t> &out) const -> bool {
			out.resize(length);

			size_t copied = 0;
			const uint64_t blocksPerPage = 0x1000 / device.blockSize;

			while (copied < length) {
				const uint64_t absoluteByte = byteOffset + copied;
				const uint64_t deviceBlock = absoluteByte / device.blockSize;
				const uint64_t pageLba = (deviceBlock / blocksPerPage) * blocksPerPage;
				const size_t pageOffset = absoluteByte - pageLba * device.blockSize;
				const size_t chunk = min<size_t>(length - copied, 0x1000 - pageOffset);
				uint64_t phys = 0;
				uint64_t virt = 0;

				if (!Fat32Utils::readDevicePage(device.deviceId, pageLba, phys, virt)) {
					printf("FAT32: Page read failed on %s lba=%lu blocks=%lu.", device.deviceName, pageLba, blocksPerPage);
					fflush(stdout);
					return false;
				}

				memcpy(out.data() + copied, reinterpret_cast<void *>(virt + pageOffset), chunk);
				Fat32Utils::freeDevicePage(phys, virt);

				copied += chunk;
			}

			return true;
		}

		auto writeBytes(const uint64_t byteOffset, const uint8_t *data, const size_t length) const -> bool {
			size_t copied = 0;
			const uint64_t blocksPerPage = 0x1000 / device.blockSize;

			while (copied < length) {
				const uint64_t absoluteByte = byteOffset + copied;
				const uint64_t deviceBlock = absoluteByte / device.blockSize;
				const uint64_t pageLba = (deviceBlock / blocksPerPage) * blocksPerPage;
				const size_t pageOffset = absoluteByte - pageLba * device.blockSize;
				const size_t chunk = min<size_t>(length - copied, 0x1000 - pageOffset);
				uint64_t phys = 0;
				uint64_t virt = 0;

				if (!Fat32Utils::readDevicePage(device.deviceId, pageLba, phys, virt)) {
					printf("FAT32: Read-before-write failed on %s lba=%lu blocks=%lu.", device.deviceName, pageLba, blocksPerPage);
					fflush(stdout);
					return false;
				}

				memcpy(reinterpret_cast<void *>(virt + pageOffset), data + copied, chunk);

				const bool success = Fat32Utils::writeDevicePage(device.deviceId, pageLba, phys);
				Fat32Utils::freeDevicePage(phys, virt);

				if (!success) {
					printf("FAT32: Page write failed on %s lba=%lu blocks=%lu.", device.deviceName, pageLba, blocksPerPage);
					fflush(stdout);
					return false;
				}

				copied += chunk;
			}

			return true;
		}

		auto bytesPerCluster() const -> uint32_t {
			return static_cast<uint32_t>(boot.bytesPerSector) * boot.sectorsPerCluster;
		}

		auto clusterByteOffset(const uint32_t cluster) const -> uint64_t {
			const uint64_t sector = firstDataSector + static_cast<uint64_t>(cluster - 2) * boot.sectorsPerCluster;

			return sector * boot.bytesPerSector;
		}

		auto directoryEntryByteOffset(const uint32_t dirCluster, const uint32_t dirOffset) const -> uint64_t {
			return clusterByteOffset(dirCluster) + dirOffset;
		}

		auto nodeIdForCluster(const uint32_t cluster) const -> uint64_t {
			return cluster != 0 ? cluster : boot.rootCluster;
		}

		auto readFatEntry(const uint32_t cluster, uint32_t &entry) const -> bool {
			if (cluster < 2 or cluster >= clusterCount + 2) {
				return false;
			}

			vector<uint8_t> bytes;
			const uint64_t fatByteOffset = (firstFatSector * boot.bytesPerSector) + static_cast<uint64_t>(cluster) * sizeof(uint32_t);

			if (!readBytes(fatByteOffset, sizeof(uint32_t), bytes)) {
				return false;
			}

			uint32_t raw = 0;
			memcpy(&raw, bytes.data(), sizeof(raw));
			entry = raw & FAT32_CLUSTER_MASK;

			return true;
		}

		auto writeFatEntry(const uint32_t cluster, const uint32_t entry) const -> bool {
			if (cluster < 2 or cluster >= clusterCount + 2) {
				return false;
			}

			uint32_t value = entry & FAT32_CLUSTER_MASK;

			for (uint32_t fat = 0; fat < boot.fatCount; ++fat) {
				const uint64_t fatByteOffset =
					((firstFatSector + static_cast<uint64_t>(fat) * boot.fatSize32) * boot.bytesPerSector) +
					static_cast<uint64_t>(cluster) * sizeof(uint32_t);

				if (!writeBytes(fatByteOffset, reinterpret_cast<const uint8_t *>(&value), sizeof(value))) {
					return false;
				}
			}

			return true;
		}

		auto readCluster(const uint32_t cluster, vector<uint8_t> &out) const -> bool {
			if (cluster < 2 or cluster >= clusterCount + 2) {
				return false;
			}

			return readBytes(clusterByteOffset(cluster), bytesPerCluster(), out);
		}

		auto writeCluster(const uint32_t cluster, const uint8_t *data) const -> bool {
			if (cluster < 2 or cluster >= clusterCount + 2) {
				return false;
			}

			return writeBytes(clusterByteOffset(cluster), data, bytesPerCluster());
		}

		auto zeroCluster(const uint32_t cluster) const -> bool {
			vector<uint8_t> zeros;

			zeros.resize(bytesPerCluster());

			return writeCluster(cluster, zeros.data());
		}

		auto allocateCluster(uint32_t &cluster) const -> bool {
			cluster = 0;

			for (uint32_t candidate = 2; candidate < clusterCount + 2; ++candidate) {
				uint32_t entry = 0;

				if (!readFatEntry(candidate, entry)) {
					return false;
				}

				if (entry != 0) {
					continue;
				}

				if (!writeFatEntry(candidate, FAT32_EOC) or !zeroCluster(candidate)) {
					return false;
				}

				cluster = candidate;

				return true;
			}

			return false;
		}

		auto readClusterChain(const uint32_t firstCluster, vector<uint8_t> &out, const uint64_t maxBytes) const -> bool {
			out.clear();

			if (firstCluster < 2) {
				return true;
			}

			uint32_t cluster = firstCluster;
			uint64_t guard = 0;

			while (cluster >= 2 and cluster < FAT32_EOC) {
				if (guard++ > clusterCount or cluster == FAT32_BAD_CLUSTER) {
					return false;
				}

				vector<uint8_t> clusterBytes;

				if (!readCluster(cluster, clusterBytes)) {
					return false;
				}

				size_t chunk = clusterBytes.size();

				if (maxBytes != 0) {
					const uint64_t remaining = maxBytes > out.size() ? maxBytes - out.size() : 0;
					chunk = min<uint64_t>(clusterBytes.size(), remaining);
				}

				out.insert(out.end(), clusterBytes.begin(), clusterBytes.begin() + chunk);

				if (maxBytes != 0 and out.size() >= maxBytes) {
					return true;
				}

				uint32_t next = 0;

				if (!readFatEntry(cluster, next)) {
					return false;
				}

				cluster = next;
			}

			return true;
		}

		auto freeClusterChain(const uint32_t firstCluster) const -> bool {
			uint32_t cluster = firstCluster;
			uint64_t guard = 0;

			while (cluster >= 2 and cluster < FAT32_EOC) {
				if (guard++ > clusterCount or cluster == FAT32_BAD_CLUSTER) {
					return false;
				}

				uint32_t next = 0;

				if (!readFatEntry(cluster, next) or !writeFatEntry(cluster, 0)) {
					return false;
				}

				cluster = next;
			}

			return true;
		}

		auto ensureFileClusters(FatDirEntry &entry, const uint32_t size) const -> bool {
			if (size == 0) {
				return true;
			}

			const uint32_t neededClusters = (size + bytesPerCluster() - 1) / bytesPerCluster();

			if (entry.firstCluster == 0) {
				if (!allocateCluster(entry.firstCluster)) {
					return false;
				}
			}

			uint32_t cluster = entry.firstCluster;

			for (uint32_t i = 1; i < neededClusters; ++i) {
				uint32_t next = 0;

				if (!readFatEntry(cluster, next)) {
					return false;
				}

				if (next >= FAT32_EOC) {
					if (!allocateCluster(next) or !writeFatEntry(cluster, next)) {
						return false;
					}
				}

				cluster = next;
			}

			return true;
		}

		auto resizeFile(FatDirEntry &entry, const uint32_t size) const -> bool {
			if (size == 0) {
				if (!freeClusterChain(entry.firstCluster)) {
					return false;
				}

				entry.firstCluster = 0;
				entry.size = 0;

				return updateDirectoryEntry(entry);
			}

			if (!ensureFileClusters(entry, size)) {
				return false;
			}

			const uint32_t keepClusters = (size + bytesPerCluster() - 1) / bytesPerCluster();
			uint32_t cluster = entry.firstCluster;

			for (uint32_t i = 1; i < keepClusters; ++i) {
				if (!readFatEntry(cluster, cluster)) {
					return false;
				}
			}

			uint32_t next = 0;

			if (!readFatEntry(cluster, next)) {
				return false;
			}

			if (next < FAT32_EOC) {
				if (!writeFatEntry(cluster, FAT32_EOC) or !freeClusterChain(next)) {
					return false;
				}
			}

			entry.size = size;

			return updateDirectoryEntry(entry);
		}

		auto directoryIsEmpty(const FatDirEntry &dir) const -> bool {
			if ((dir.attr & FAT_ATTR_DIRECTORY) == 0) {
				return false;
			}

			vector<FatDirEntry> entries;

			if (!readDirectoryEntries(dir.firstCluster, entries)) {
				return false;
			}

			for (const auto &entry : entries) {
				if (entry.name != "." and entry.name != "..") {
					return false;
				}
			}

			return true;
		}

		auto updateDirectoryEntry(const FatDirEntry &entry) const -> bool {
			vector<uint8_t> bytes;
			const uint64_t byteOffset = directoryEntryByteOffset(entry.dirCluster, entry.dirOffset);

			if (!readBytes(byteOffset, sizeof(FatDirEntryRaw), bytes)) {
				return false;
			}

			auto *raw = reinterpret_cast<FatDirEntryRaw *>(bytes.data());

			raw->attr = entry.attr;
			raw->firstClusterHigh = static_cast<uint16_t>(entry.firstCluster >> 16);
			raw->firstClusterLow = static_cast<uint16_t>(entry.firstCluster);
			raw->fileSize = entry.size;

			return writeBytes(byteOffset, bytes.data(), bytes.size());
		}

		auto deleteDirectoryEntry(const FatDirEntry &entry) const -> bool {
			vector<uint8_t> clusterBytes;

			if (!readCluster(entry.dirCluster, clusterBytes) or entry.dirOffset + sizeof(FatDirEntryRaw) > clusterBytes.size()) {
				return false;
			}

			uint32_t firstOffset = entry.dirOffset;

			while (firstOffset >= sizeof(FatDirEntryRaw)) {
				const uint32_t previousOffset = firstOffset - sizeof(FatDirEntryRaw);
				const auto *previous = reinterpret_cast<const FatDirEntryRaw *>(clusterBytes.data() + previousOffset);

				if (previous->attr != FAT_ATTR_LFN) {
					break;
				}

				firstOffset = previousOffset;
			}

			for (uint32_t offset = firstOffset; offset <= entry.dirOffset; offset += sizeof(FatDirEntryRaw)) {
				clusterBytes[offset] = 0xE5;
			}

			return writeCluster(entry.dirCluster, clusterBytes.data());
		}

		auto addDirectoryEntries(const FatDirEntry &dir, const vector<FatDirEntryRaw> &rawEntries, FatDirEntry &created) const -> bool {
			if (rawEntries.empty()) {
				return false;
			}

			vector<uint8_t> bytes;

			if (!readClusterChain(dir.firstCluster, bytes, 0)) {
				return false;
			}

			uint32_t offset = 0;
			uint32_t runStart = 0;
			uint32_t runLength = 0;

			while (offset + sizeof(FatDirEntryRaw) <= bytes.size()) {
				const auto *candidate = reinterpret_cast<const FatDirEntryRaw *>(bytes.data() + offset);

				if (candidate->name[0] == 0x00 or candidate->name[0] == 0xE5) {
					if (runLength == 0) {
						runStart = offset;
					}

					++runLength;

					if (runLength >= rawEntries.size()) {
						return writeDirectoryEntriesAtOffset(dir, runStart, rawEntries, created);
					}
				} else {
					runLength = 0;
				}

				offset += sizeof(FatDirEntryRaw);
			}

			uint32_t cluster = dir.firstCluster;

			for (;;) {
				uint32_t next = 0;

				if (!readFatEntry(cluster, next)) {
					return false;
				}

				if (next >= FAT32_EOC) {
					if (!allocateCluster(next) or !writeFatEntry(cluster, next)) {
						return false;
					}

					return writeDirectoryEntriesAtOffset(dir, offset, rawEntries, created);
				}

				cluster = next;
			}
		}

		auto writeDirectoryEntriesAtOffset(const FatDirEntry &dir, const uint32_t offset, const vector<FatDirEntryRaw> &rawEntries, FatDirEntry &created) const -> bool {
			const uint32_t clusterBytes = bytesPerCluster();
			uint32_t currentOffset = offset;

			for (const auto &raw : rawEntries) {
				uint32_t cluster = dir.firstCluster;
				uint32_t remaining = currentOffset;

				while (remaining >= clusterBytes) {
					uint32_t next = 0;

					if (!readFatEntry(cluster, next) or next >= FAT32_EOC) {
						return false;
					}

					cluster = next;
					remaining -= clusterBytes;
				}

				if (!writeBytes(directoryEntryByteOffset(cluster, remaining), reinterpret_cast<const uint8_t *>(&raw), sizeof(raw))) {
					return false;
				}

				currentOffset += sizeof(FatDirEntryRaw);
			}

			uint32_t entryCluster = 0;
			uint32_t entryOffset = 0;

			if (!resolveDirectoryOffset(dir.firstCluster, offset + ((rawEntries.size() - 1) * sizeof(FatDirEntryRaw)), entryCluster, entryOffset)) {
				return false;
			}

			created = makeEntry(rawEntries.back(), "", entryCluster, entryOffset);

			return true;
		}

		auto resolveDirectoryOffset(const uint32_t dirCluster, const uint32_t offset, uint32_t &entryCluster, uint32_t &entryOffset) const -> bool {
			const uint32_t clusterBytes = bytesPerCluster();
			uint32_t cluster = dirCluster;
			uint32_t remaining = offset;

			while (remaining >= clusterBytes) {
				uint32_t next = 0;

				if (!readFatEntry(cluster, next) or next >= FAT32_EOC) {
					return false;
				}

				cluster = next;
				remaining -= clusterBytes;
			}

			entryCluster = cluster;
			entryOffset = remaining;

			return true;
		}

		auto readDirectoryEntries(const uint32_t cluster, vector<FatDirEntry> &out) const -> bool {
			vector<uint8_t> bytes;

			if (!readClusterChain(cluster, bytes, 0)) {
				return false;
			}

			string longName;

			for (size_t offset = 0; offset + sizeof(FatDirEntryRaw) <= bytes.size(); offset += sizeof(FatDirEntryRaw)) {
				const auto *raw = reinterpret_cast<const FatDirEntryRaw *>(bytes.data() + offset);
				const uint8_t first = raw->name[0];

				if (first == 0x00) {
					break;
				}

				if (first == 0xE5) {
					longName.clear();
					continue;
				}

				if (raw->attr == FAT_ATTR_LFN) {
					longName = readLongNamePart(bytes.data() + offset) + longName;
					continue;
				}

				if ((raw->attr & FAT_ATTR_VOLUME_ID) != 0) {
					longName.clear();
					continue;
				}

				uint32_t entryCluster = 0;
				uint32_t entryOffset = 0;

				if (!resolveDirectoryOffset(cluster, offset, entryCluster, entryOffset)) {
					return false;
				}

				out.push_back(makeEntry(*raw, longName, entryCluster, entryOffset));
				longName.clear();
			}

			return true;
		}

		static auto readLongNamePart(const uint8_t *entry) -> string {
			string out;
			const uint8_t offsets[] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

			for (uint8_t offset : offsets) {
				uint16_t ch = 0;
				memcpy(&ch, entry + offset, sizeof(ch));

				if (ch == 0x0000 or ch == 0xFFFF) {
					break;
				}

				out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
			}

			return out;
		}

		static auto shortName(const uint8_t name[11]) -> string {
			string base;
			string ext;

			for (uint32_t i = 0; i < 8 and name[i] != ' '; ++i) {
				base.push_back(static_cast<char>(name[i]));
			}

			for (uint32_t i = 8; i < 11 and name[i] != ' '; ++i) {
				ext.push_back(static_cast<char>(name[i]));
			}

			if (!ext.empty()) {
				return base + "." + ext;
			}

			return base;
		}

		static auto splitParentPath(const string &path, string &parentPath, string &name) -> bool {
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

		static auto makeShortName(const string &name, uint8_t out[11]) -> bool {
			memset(out, ' ', 11);

			if (name.empty() or name == "." or name == "..") {
				return false;
			}

			const size_t dot = name.find('.');
			const string base = dot == string::npos ? name : name.substr(0, dot);
			const string ext = dot == string::npos ? string() : name.substr(dot + 1);

			if (base.empty() or base.size() > 8 or ext.size() > 3 or (dot != string::npos and name.find('.', dot + 1) != string::npos)) {
				return false;
			}

			auto validChar = [](const char ch) {
				const unsigned char uch = static_cast<unsigned char>(ch);

				return isalnum(uch) or ch == '_' or ch == '-' or ch == '$' or ch == '~';
			};

			for (size_t i = 0; i < base.size(); ++i) {
				if (!validChar(base[i])) {
					return false;
				}

				out[i] = static_cast<uint8_t>(toupper(static_cast<unsigned char>(base[i])));
			}

			for (size_t i = 0; i < ext.size(); ++i) {
				if (!validChar(ext[i])) {
					return false;
				}

				out[8 + i] = static_cast<uint8_t>(toupper(static_cast<unsigned char>(ext[i])));
			}

			return true;
		}

		static auto lfnChecksum(const uint8_t shortName[11]) -> uint8_t {
			uint8_t sum = 0;

			for (uint32_t i = 0; i < 11; ++i) {
				sum = static_cast<uint8_t>(((sum & 1U) ? 0x80U : 0U) + (sum >> 1U) + shortName[i]);
			}

			return sum;
		}

		static void writeLfnChar(uint8_t *entry, const uint8_t offset, const uint16_t value) {
			memcpy(entry + offset, &value, sizeof(value));
		}

		static auto makeLfnEntry(const string &name, const uint32_t part, const uint32_t partCount, const uint8_t checksum) -> FatDirEntryRaw {
			auto raw = FatDirEntryRaw();
			auto *entry = reinterpret_cast<uint8_t *>(&raw);
			const uint8_t offsets[] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
			const uint32_t start = part * 13;

			memset(entry, 0xff, sizeof(FatDirEntryRaw));
			entry[0] = static_cast<uint8_t>(part + 1);

			if (part + 1 == partCount) {
				entry[0] |= 0x40;
			}

			entry[11] = FAT_ATTR_LFN;
			entry[12] = 0;
			entry[13] = checksum;
			entry[26] = 0;
			entry[27] = 0;

			for (uint32_t i = 0; i < 13; ++i) {
				const uint32_t index = start + i;
				const uint16_t value = index < name.size() ? static_cast<uint8_t>(name[index]) : 0x0000;
				writeLfnChar(entry, offsets[i], value);

				if (index >= name.size()) {
					for (uint32_t j = i + 1; j < 13; ++j) {
						writeLfnChar(entry, offsets[j], 0xffff);
					}

					break;
				}
			}

			return raw;
		}

		static auto makeShortAlias(const string &name, uint8_t out[11]) -> bool {
			if (makeShortName(name, out)) {
				return true;
			}

			string base;
			string ext;
			const size_t dot = name.find_last_of('.');
			const string rawBase = dot == string::npos ? name : name.substr(0, dot);
			const string rawExt = dot == string::npos ? string() : name.substr(dot + 1);

			auto appendClean = [](string &dst, const string &src, const size_t maxLen) {
				for (char ch : src) {
					if (dst.size() == maxLen) {
						break;
					}

					const unsigned char uch = static_cast<unsigned char>(ch);

					if (isalnum(uch) or ch == '_' or ch == '-' or ch == '$') {
						dst.push_back(static_cast<char>(toupper(uch)));
					}
				}
			};

			appendClean(base, rawBase, 6);
			appendClean(ext, rawExt, 3);

			if (base.empty()) {
				base = "FILE";
			}

			base = base.substr(0, min<size_t>(base.size(), 6)) + "~1";

			memset(out, ' ', 11);
			memcpy(out, base.data(), min<size_t>(base.size(), 8));
			memcpy(out + 8, ext.data(), min<size_t>(ext.size(), 3));

			return true;
		}

		static auto makeDirectoryEntries(const string &name, const uint8_t attr, const uint32_t firstCluster, const uint32_t size, vector<FatDirEntryRaw> &entries) -> bool {
			if (name.empty() or name.size() >= VFS_MAX_NAME_LENGTH) {
				return false;
			}

			auto raw = FatDirEntryRaw();

			if (!makeShortAlias(name, raw.name)) {
				return false;
			}

			raw.attr = attr;
			raw.firstClusterHigh = static_cast<uint16_t>(firstCluster >> 16);
			raw.firstClusterLow = static_cast<uint16_t>(firstCluster);
			raw.fileSize = size;

			entries.clear();

			uint8_t exactShort[11] {};
			const bool exactShortValid = makeShortName(name, exactShort);
			const bool needsLfn = !exactShortValid or shortName(exactShort) != name;

			if (needsLfn) {
				const uint32_t partCount = (name.size() + 12) / 13;
				const uint8_t checksum = lfnChecksum(raw.name);

				for (uint32_t part = partCount; part > 0; --part) {
					entries.push_back(makeLfnEntry(name, part - 1, partCount, checksum));
				}
			}

			entries.push_back(raw);

			return true;
		}

		static auto makeEntry(const FatDirEntryRaw &raw, const string &longName, const uint32_t dirCluster, const uint32_t dirOffset) -> FatDirEntry {
			auto entry = FatDirEntry();

			entry.name = longName.empty() ? shortName(raw.name) : longName;
			entry.attr = raw.attr;
			entry.firstCluster = (static_cast<uint32_t>(raw.firstClusterHigh) << 16) | raw.firstClusterLow;
			entry.size = raw.fileSize;
			entry.dirCluster = dirCluster;
			entry.dirOffset = dirOffset;

			return entry;
		}

		StorageFsProbeDeviceMsgData device {};
		FatBootSector boot {};
		uint64_t totalSectors {};
		uint64_t firstFatSector {};
		uint64_t firstDataSector {};
		uint64_t clusterCount {};
	};
}

#endif
