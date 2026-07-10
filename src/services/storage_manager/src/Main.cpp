#include "StorageProtocol.hpp"
#include "StorageManager.hpp"
#include "PartitionRules.hpp"

#include "bits/linux/linux_sched.h"
#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

namespace {
	uint64_t storagePort = 0;
	uint64_t nvmePort = 0;
	uint64_t nvmeReplyPort = 0;
	uint64_t nextNvmeRequestId = 1;
	uint64_t nextBlockDeviceId = 1;
	vector<BlockDevice> blockDevices;
	vector<FsHandler> fsHandlers;
	mutex storageMutex;
	mutex blockRequestMutex;
	mutex nvmeRequestIdMutex;
	constexpr uint32_t PARTITION_READ_ATTEMPTS = 10;
	constexpr useconds_t PARTITION_READ_RETRY_DELAY_US = 100000;
	constexpr useconds_t PARTITION_PROBE_SETTLE_DELAY_US = 100000;

	template<typename Reply>
	auto receiveBlockReply(const uint64_t replyType, const uint64_t requestId, Reply &reply) -> int {
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		uint64_t acceptedType = replyType;
		auto filter = filter_options();
		filter.whiteListTypes = &acceptedType;
		filter.whiteListCount = 1;

		for (;;) {
			reply = {};
			const int ret = receive_horizonos_message(nvmeReplyPort, &recv, &filter);

			// A transient scheduler lookup failure leaves the request in flight.
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

auto StorageManagerUtils::allocateBlockDeviceIdLocked() -> uint64_t {
		return nextBlockDeviceId++;
	}

	auto StorageManagerUtils::allocateNvmeRequestId() -> uint64_t {
		const scoped_lock lock(nvmeRequestIdMutex);
		return nextNvmeRequestId++;
	}

	auto StorageManagerUtils::validName(const char *name, const size_t length, const size_t maxLength, string &out) -> bool {
		if (length == 0 or length > maxLength or name[length - 1] != '\0') {
			return false;
		}

		out.assign(name, length - 1);

		return true;
	}

	void StorageManagerUtils::fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());

		memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto StorageManagerUtils::gptNameToString(const uint16_t *name, const size_t charCount) -> string {
		string out;

		for (size_t i = 0; i < charCount and name[i] != 0; ++i) {
			const uint16_t ch = name[i];

			out.push_back(ch >= 0x20 and ch <= 0x7E ? static_cast<char>(ch) : '_');
		}

		return out;
	}

	auto StorageManagerUtils::registerWithNameRegistry(const char *name) -> bool {
		auto msg = hos_msg();
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		StorageManagerUtils::fillName(data.name, sizeof(data.name), data.nameLength, name);

		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storagePort, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto replyMsg = hos_msg();

		replyMsg.buffer = &reply;
		replyMsg.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(storagePort, &replyMsg, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto StorageManagerUtils::waitForService(const char *name) -> GetReplyMsgData {
		for (;;) {
			auto check = CheckMsgData();

			StorageManagerUtils::fillName(check.name, sizeof(check.name), check.nameLength, name);

			auto checkMsg = hos_msg();

			checkMsg.type = CHECK_MSG_TYPE;
			checkMsg.port = 1;
			checkMsg.buffer = &check;
			checkMsg.length = sizeof(check);

			send_horizonos_message(storagePort, 1, &checkMsg);

			auto checkReply = CheckReplyMsgData();
			auto recv = hos_msg();

			recv.buffer = &checkReply;
			recv.length = sizeof(checkReply);

			auto filter = filter_options();

			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(storagePort, &recv, &filter);

			delete[] filter.whiteListTypes;

			if (ret == 0 and checkReply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();

		StorageManagerUtils::fillName(get.name, sizeof(get.name), get.nameLength, name);

		auto getMsg = hos_msg();

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &get;
		getMsg.length = sizeof(get);

		send_horizonos_message(storagePort, 1, &getMsg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(storagePort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply;
	}

	auto StorageManagerUtils::findBlockDeviceLocked(const uint64_t id) -> BlockDevice * {
		const auto it = ranges::find_if(blockDevices, [&](const BlockDevice &dev) -> bool {
			return dev.id == id;
		});

		return it == blockDevices.end() ? nullptr : &(*it);
	}

	auto StorageManagerUtils::transferBlockCount(const BlockDevice &device, const uint32_t pageCount) -> uint64_t {
		return (static_cast<uint64_t>(pageCount) * 0x1000) / device.blockSize;
	}

	auto StorageManagerUtils::translateToBlockLocked(const BlockDevice &device, const uint64_t lba, const uint32_t pageCount, BlockDevice &out) -> bool {
		const uint64_t blocks = StorageManagerUtils::transferBlockCount(device, pageCount);

		if (blocks == 0 or lba >= device.blockCount or blocks > device.blockCount - lba) {
			return false;
		}

		if (device.kind == BlockDeviceKind::WholeDisk) {
			out = device;
			out.parentStartLba = lba;

			return true;
		}

		const BlockDevice *parent = StorageManagerUtils::findBlockDeviceLocked(device.parentId);

		if (parent == nullptr or device.parentStartLba + lba >= parent->blockCount or blocks > parent->blockCount - (device.parentStartLba + lba)) {
			return false;
		}

		out = *parent;
		out.parentStartLba = device.parentStartLba + lba;

		return true;
	}

	auto StorageManagerUtils::currentCpuId() -> uint64_t {
		const int cpuId = sched_getcpu();

		return cpuId < 0 ? 0 : static_cast<uint64_t>(cpuId);
	}

	void StorageManagerUtils::applyDefaultTransport(BlockDevice &device) {
		if (device.readMsgBase == 0) {
			device.readMsgBase = NVME_READ_MSG_BASE;
		}

		if (device.writeMsgBase == 0) {
			device.writeMsgBase = NVME_WRITE_MSG_BASE;
		}

		if (device.flushMsgBase == 0) {
			device.flushMsgBase = NVME_FLUSH_MSG_BASE;
		}

		if (device.readReplyMsgBase == 0) {
			device.readReplyMsgBase = NVME_REPLY_READ_MSG_BASE;
		}

		if (device.writeReplyMsgBase == 0) {
			device.writeReplyMsgBase = NVME_REPLY_WRITE_MSG_BASE;
		}

		if (device.flushReplyMsgBase == 0) {
			device.flushReplyMsgBase = NVME_REPLY_FLUSH_MSG_BASE;
		}
	}

	auto StorageManagerUtils::blockRead(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = StorageManagerUtils::currentCpuId();
		const uint64_t requestId = StorageManagerUtils::allocateNvmeRequestId();

		auto data = NvmeReadMsgData();

		data.replyPort = nvmeReplyPort;
		data.requestId = requestId;
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;
		data.lba = lba;
		data.pageCount = pageCount;

		memcpy(data.pagePhysArray, pagePhysArray, pageCount * sizeof(uint64_t));

		auto msg = hos_msg();

		msg.type = device.readMsgBase + cpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		//printf("Storage: Sending NVMe read for %s ctrl=%u nsid=%u lba=%lu pages=%u type=%lu.", device.name.c_str(), device.controllerId, device.nsid, lba, pageCount, msg.type);
		//fflush(stdout);

		if (send_horizonos_message(nvmeReplyPort, device.driverPort, &msg) != 0) {
			printf("Storage: Failed to send block read for %s.", device.name.c_str());
			fflush(stdout);

			return false;
		}

		auto reply = NvmeReadReplyMsgData();
		const int ret = receiveBlockReply(device.readReplyMsgBase + cpuId, requestId, reply);

		//printf("Storage: NVMe read reply for %s ret=%d success=%d.", device.name.c_str(), ret, reply.success);
		//fflush(stdout);

		const bool success = ret == 0 and reply.requestId == requestId and reply.success;

		if (!success) {
			printf("Storage: Block read failed for %s lba=%lu pages=%u ret=%d request=%lu replyRequest=%lu success=%d.",
			       device.name.c_str(),
			       lba,
			       pageCount,
			       ret,
			       requestId,
			       reply.requestId,
			       reply.success);
			fflush(stdout);
		}

		return success;
	}

	auto StorageManagerUtils::blockWrite(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = StorageManagerUtils::currentCpuId();
		const uint64_t requestId = StorageManagerUtils::allocateNvmeRequestId();

		auto data = NvmeWriteMsgData();

		data.replyPort = nvmeReplyPort;
		data.requestId = requestId;
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;
		data.lba = lba;
		data.pageCount = pageCount;

		memcpy(data.pagePhysArray, pagePhysArray, pageCount * sizeof(uint64_t));

		auto msg = hos_msg();

		msg.type = device.writeMsgBase + cpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(nvmeReplyPort, device.driverPort, &msg) != 0) {
			return false;
		}

		auto reply = NvmeWriteReplyMsgData();
		const int ret = receiveBlockReply(device.writeReplyMsgBase + cpuId, requestId, reply);

		const bool success = ret == 0 and reply.requestId == requestId and reply.success;

		if (!success) {
			printf("Storage: Block write failed for %s lba=%lu pages=%u ret=%d request=%lu replyRequest=%lu success=%d.",
			       device.name.c_str(),
			       lba,
			       pageCount,
			       ret,
			       requestId,
			       reply.requestId,
			       reply.success);
			fflush(stdout);
		}

		return success;
	}

	auto StorageManagerUtils::blockFlush(const BlockDevice &device) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = StorageManagerUtils::currentCpuId();
		const uint64_t requestId = StorageManagerUtils::allocateNvmeRequestId();

		auto data = NvmeFlushMsgData();

		data.replyPort = nvmeReplyPort;
		data.requestId = requestId;
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;

		auto msg = hos_msg();

		msg.type = device.flushMsgBase + cpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(nvmeReplyPort, device.driverPort, &msg) != 0) {
			return false;
		}

		auto reply = NvmeFlushReplyMsgData();
		const int ret = receiveBlockReply(device.flushReplyMsgBase + cpuId, requestId, reply);

		return ret == 0 and reply.requestId == requestId and reply.success;
	}

	auto StorageManagerUtils::readOnePage(const BlockDevice &device, const uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool {
		if (allocPhysPage(&phys) != 0) {
			return false;
		}

		if (mmap_phys(phys, 0x1000, &virt, false) != 0) {
			freePhysPage(phys);

			phys = 0;

			return false;
		}

		const uint64_t pages[1] { phys };

		for (uint32_t attempt = 0; attempt < PARTITION_READ_ATTEMPTS; ++attempt) {
			memset(reinterpret_cast<void *>(virt), 0, 0x1000);

			if (StorageManagerUtils::blockRead(device, lba, pages, 1)) {
				return true;
			}

			if (attempt + 1 < PARTITION_READ_ATTEMPTS) {
				usleep(PARTITION_READ_RETRY_DELAY_US);
			}
		}

		munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		freePhysPage(phys);

		phys = 0;
		virt = 0;

		return false;
	}

	void StorageManagerUtils::freeOnePage(const uint64_t phys, const uint64_t virt) {
		if (virt != 0) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	auto StorageManagerUtils::readDeviceBytes(const BlockDevice &device, const uint64_t byteOffset, const size_t length, vector<uint8_t> &out) -> bool {
		out.clear();

		if (device.blockSize == 0 or device.blockSize > 0x1000 or (0x1000 % device.blockSize) != 0 or
		    device.blockCount > UINT64_MAX / device.blockSize or
		    byteOffset > device.blockCount * static_cast<uint64_t>(device.blockSize) or
		    length > device.blockCount * static_cast<uint64_t>(device.blockSize) - byteOffset) {
			return false;
		}

		out.resize(length);
		const uint64_t blocksPerPage = 0x1000 / device.blockSize;
		size_t copied = 0;

		while (copied < length) {
			const uint64_t absoluteByte = byteOffset + copied;
			const uint64_t block = absoluteByte / device.blockSize;
			const uint64_t pageLba = (block / blocksPerPage) * blocksPerPage;
			const size_t pageOffset = static_cast<size_t>(absoluteByte - pageLba * device.blockSize);
			const size_t chunk = min<size_t>(length - copied, 0x1000 - pageOffset);
			uint64_t phys = 0;
			uint64_t virt = 0;

			if (!StorageManagerUtils::readOnePage(device, pageLba, phys, virt)) {
				return false;
			}

			memcpy(out.data() + copied, reinterpret_cast<const void *>(virt + pageOffset), chunk);
			StorageManagerUtils::freeOnePage(phys, virt);
			copied += chunk;
		}

		return true;
	}

	void StorageManagerUtils::notifyFsHandlers(const BlockDevice &device) {
		vector<FsHandler> handlers;

		{
			const scoped_lock lock(storageMutex);
			handlers = fsHandlers;
		}

		for (const auto &handler : handlers) {
			auto data = StorageFsProbeDeviceMsgData();
			data.deviceId = device.id;
			data.blockCount = device.blockCount;
			data.blockSize = device.blockSize;
			StorageManagerUtils::fillName(data.deviceName, sizeof(data.deviceName), data.deviceNameLength, device.name);

			auto msg = hos_msg();
			msg.type = STORAGE_FS_PROBE_DEVICE_MSG_TYPE;
			msg.port = handler.port;
			msg.buffer = &data;
			msg.length = sizeof(data);

			send_horizonos_message(storagePort, handler.port, &msg);
		}
	}

	void StorageManagerUtils::probeGpt(const BlockDevice &rawDevice) {
		uint64_t headerPhys = 0;
		uint64_t headerVirt = 0;

		printf("Storage: Probing GPT on %s.", rawDevice.name.c_str());
		fflush(stdout);

		if (!StorageManagerUtils::readOnePage(rawDevice, 1, headerPhys, headerVirt)) {
			printf("Storage: Failed to read GPT header from %s.", rawDevice.name.c_str());
			fflush(stdout);

			return;
		}

		const auto *header = reinterpret_cast<const GptHeader *>(headerVirt);

		if (header->signature != GPT_SIGNATURE or rawDevice.blockSize == 0 or rawDevice.blockCount > UINT64_MAX / rawDevice.blockSize or
		    header->headerSize < sizeof(GptHeader) or header->headerSize > rawDevice.blockSize or
		    header->currentLba != 1 or header->firstUsableLba > header->lastUsableLba or header->lastUsableLba >= rawDevice.blockCount or
		    header->partitionEntryLba >= rawDevice.blockCount or
		    header->partitionEntrySize < sizeof(GptPartitionEntry) or header->partitionEntrySize > 0x1000 or (header->partitionEntrySize % 8) != 0) {
			StorageManagerUtils::freeOnePage(headerPhys, headerVirt);

			printf("Storage: %s has no GPT header.", rawDevice.name.c_str());
			fflush(stdout);

			return;
		}

		constexpr uint64_t MAX_GPT_ENTRY_BYTES = 4ULL * 1024 * 1024;
		const uint32_t entrySize = header->partitionEntrySize;
		const uint32_t maxEntries = partition_rules::boundedGptEntryCount(header->partitionEntryCount, entrySize, MAX_GPT_ENTRY_BYTES);
		const uint64_t entryArrayByteOffset = header->partitionEntryLba * static_cast<uint64_t>(rawDevice.blockSize);
		const size_t entryArrayBytes = static_cast<size_t>(maxEntries) * entrySize;
		vector<uint8_t> entryBytes;
		uint32_t created = 0;

		if (maxEntries != 0 and StorageManagerUtils::readDeviceBytes(rawDevice, entryArrayByteOffset, entryArrayBytes, entryBytes)) {
			for (uint32_t globalEntryIndex = 0; globalEntryIndex < maxEntries; ++globalEntryIndex) {
				const auto *entry = reinterpret_cast<const GptPartitionEntry *>(entryBytes.data() + static_cast<size_t>(globalEntryIndex) * entrySize);
				const bool empty = ranges::all_of(entry->partitionTypeGuid.bytes, [](const uint8_t byte) -> bool {
					return byte == 0;
				});

				if (!empty and partition_rules::validGptEntry(rawDevice.blockCount,
				                                                     header->firstUsableLba,
				                                                     header->lastUsableLba,
				                                                     entry->firstLba,
				                                                     entry->lastLba)) {
					BlockDevice partition {};

					partition.kind = BlockDeviceKind::Partition;
					partition.driverPort = rawDevice.driverPort;
					partition.controllerId = rawDevice.controllerId;
					partition.nsid = rawDevice.nsid;
					partition.blockCount = entry->lastLba - entry->firstLba + 1;
					partition.blockSize = rawDevice.blockSize;
					partition.maxPagesPerRequest = rawDevice.maxPagesPerRequest;
					partition.transport = rawDevice.transport;
					partition.readMsgBase = rawDevice.readMsgBase;
					partition.writeMsgBase = rawDevice.writeMsgBase;
					partition.flushMsgBase = rawDevice.flushMsgBase;
					partition.readReplyMsgBase = rawDevice.readReplyMsgBase;
					partition.writeReplyMsgBase = rawDevice.writeReplyMsgBase;
					partition.flushReplyMsgBase = rawDevice.flushReplyMsgBase;
					partition.parentId = rawDevice.id;
					partition.parentStartLba = entry->firstLba;
					partition.partitionType = entry->partitionTypeGuid;
					partition.partitionId = entry->uniquePartitionGuid;
					partition.name = rawDevice.name + "p" + to_string(globalEntryIndex + 1);
					partition.label = StorageManagerUtils::gptNameToString(entry->name, 36);

					{
						const scoped_lock lock(storageMutex);
						partition.id = StorageManagerUtils::allocateBlockDeviceIdLocked();
						blockDevices.push_back(partition);
					}

					printf("Storage: Registered partition %s id=%lu start=%lu blocks=%lu.", partition.name.c_str(), partition.id, partition.parentStartLba, partition.blockCount);
					fflush(stdout);

					StorageManagerUtils::notifyFsHandlers(partition);
					++created;
				}
			}
		} else if (maxEntries != 0) {
			printf("Storage: Failed to read GPT entry array from %s.", rawDevice.name.c_str());
			fflush(stdout);
		}

		StorageManagerUtils::freeOnePage(headerPhys, headerVirt);

	printf("Storage: GPT probe for %s created %u partition(s).", rawDevice.name.c_str(), created);
	fflush(stdout);
}

void StorageManagerUtils::probeMbr(const BlockDevice &rawDevice) {
	uint64_t mbrPhys = 0;
	uint64_t mbrVirt = 0;

	printf("Storage: Probing MBR on %s.", rawDevice.name.c_str());
	fflush(stdout);

	if (!StorageManagerUtils::readOnePage(rawDevice, 0, mbrPhys, mbrVirt)) {
		printf("Storage: Failed to read MBR from %s.", rawDevice.name.c_str());
		fflush(stdout);

		return;
	}

	const auto *mbr = reinterpret_cast<const MbrSector *>(mbrVirt);

	if (mbr->signature != MBR_BOOT_SIGNATURE) {
		StorageManagerUtils::freeOnePage(mbrPhys, mbrVirt);

		printf("Storage: %s has no MBR signature.", rawDevice.name.c_str());
		fflush(stdout);

		return;
	}

	uint32_t created = 0;
	uint32_t logicalIndex = 5;
	vector<pair<uint64_t, uint64_t>> extendedPartitions;
	const auto registerPartition = [&](const uint8_t type, const uint64_t firstLba, const uint64_t sectorCount, const uint32_t partitionIndex) -> bool {
		if (!partition_rules::validRange(rawDevice.blockCount, firstLba, sectorCount)) {
			printf("Storage: Ignoring invalid MBR partition %u on %s start=%lu sectors=%lu.", partitionIndex, rawDevice.name.c_str(), firstLba, sectorCount);
			fflush(stdout);
			return false;
		}

		BlockDevice partition {};

		partition.kind = BlockDeviceKind::Partition;
		partition.driverPort = rawDevice.driverPort;
		partition.controllerId = rawDevice.controllerId;
		partition.nsid = rawDevice.nsid;
		partition.blockCount = sectorCount;
		partition.blockSize = rawDevice.blockSize;
		partition.maxPagesPerRequest = rawDevice.maxPagesPerRequest;
		partition.transport = rawDevice.transport;
		partition.readMsgBase = rawDevice.readMsgBase;
		partition.writeMsgBase = rawDevice.writeMsgBase;
		partition.flushMsgBase = rawDevice.flushMsgBase;
		partition.readReplyMsgBase = rawDevice.readReplyMsgBase;
		partition.writeReplyMsgBase = rawDevice.writeReplyMsgBase;
		partition.flushReplyMsgBase = rawDevice.flushReplyMsgBase;
		partition.parentId = rawDevice.id;
		partition.parentStartLba = firstLba;
		partition.name = rawDevice.name + "p" + to_string(partitionIndex);
		partition.label = partition.name;

		{
			const scoped_lock lock(storageMutex);
			partition.id = StorageManagerUtils::allocateBlockDeviceIdLocked();
			blockDevices.push_back(partition);
		}

		printf("Storage: Registered MBR partition %s id=%lu type=0x%02x start=%lu blocks=%lu.", partition.name.c_str(), partition.id, type, partition.parentStartLba, partition.blockCount);
		fflush(stdout);

		StorageManagerUtils::notifyFsHandlers(partition);
		++created;
		return true;
	};

	for (uint32_t i = 0; i < 4; ++i) {
		const MbrPartitionEntry &entry = mbr->partitions[i];

		if (entry.type == 0 or entry.type == 0xEE or entry.sectorCount == 0) {
			continue;
		}

		if (partition_rules::isExtendedMbrType(entry.type)) {
			extendedPartitions.emplace_back(entry.firstLba, entry.sectorCount);
			continue;
		}

		registerPartition(entry.type, entry.firstLba, entry.sectorCount, i + 1);
	}

	StorageManagerUtils::freeOnePage(mbrPhys, mbrVirt);

	for (const auto &[extendedBase, extendedLength] : extendedPartitions) {
		if (!partition_rules::validRange(rawDevice.blockCount, extendedBase, extendedLength)) {
			continue;
		}

		uint64_t ebrLba = extendedBase;
		vector<uint64_t> visited;

		for (uint32_t link = 0; link < 128; ++link) {
			if (ranges::find(visited, ebrLba) != visited.end()) {
				printf("Storage: Stopped cyclic EBR chain on %s at LBA %lu.", rawDevice.name.c_str(), ebrLba);
				fflush(stdout);
				break;
			}

			visited.push_back(ebrLba);
			uint64_t ebrPhys = 0;
			uint64_t ebrVirt = 0;

			if (!StorageManagerUtils::readOnePage(rawDevice, ebrLba, ebrPhys, ebrVirt)) {
				break;
			}

			const auto *ebr = reinterpret_cast<const MbrSector *>(ebrVirt);
			const MbrSector ebrCopy = *ebr;
			StorageManagerUtils::freeOnePage(ebrPhys, ebrVirt);

			if (ebrCopy.signature != MBR_BOOT_SIGNATURE) {
				break;
			}

			const auto &logical = ebrCopy.partitions[0];

			if (logical.type != 0 and !partition_rules::isExtendedMbrType(logical.type)) {
				uint64_t logicalStart = 0;

				if (partition_rules::logicalRange(rawDevice.blockCount,
				                                  extendedBase,
				                                  extendedLength,
				                                  ebrLba,
				                                  logical.firstLba,
				                                  logical.sectorCount,
				                                  logicalStart)) {
					registerPartition(logical.type, logicalStart, logical.sectorCount, logicalIndex++);
				}
			}

			const auto &next = ebrCopy.partitions[1];

			if (!partition_rules::isExtendedMbrType(next.type) or next.sectorCount == 0 or next.firstLba > UINT64_MAX - extendedBase) {
				break;
			}

			const uint64_t nextEbr = extendedBase + next.firstLba;

			if (nextEbr < extendedBase or nextEbr - extendedBase >= extendedLength) {
				break;
			}

			ebrLba = nextEbr;
		}
	}

	printf("Storage: MBR probe for %s created %u partition(s).", rawDevice.name.c_str(), created);
	fflush(stdout);
}

namespace {
	[[noreturn]] auto blockRegistrationHandler(void */*unused*/) -> void * {
		auto data = StorageRegisterBlockDeviceMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_BLOCK_DEVICE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			string name;
			auto reply = StorageRegisterBlockDeviceReplyMsgData();
			BlockDevice registeredDevice {};
			bool shouldProbe = false;

			if (StorageManagerUtils::validName(data.name, data.nameLength, sizeof(data.name), name) and data.blockSize != 0 and data.blockCount != 0) {
				BlockDevice device {};

				device.kind = BlockDeviceKind::WholeDisk;
				device.driverPort = data.driverPort;
				device.controllerId = data.controllerId;
				device.nsid = data.nsid;
				device.blockCount = data.blockCount;
				device.blockSize = data.blockSize;
				device.maxPagesPerRequest = data.maxPagesPerRequest == 0 ? STORAGE_MAX_PAGES_PER_MSG : min<uint32_t>(data.maxPagesPerRequest, STORAGE_MAX_PAGES_PER_MSG);
				device.transport = data.transport;
				device.readMsgBase = data.readMsgBase;
				device.writeMsgBase = data.writeMsgBase;
				device.flushMsgBase = data.flushMsgBase;
				device.readReplyMsgBase = data.readReplyMsgBase;
				device.writeReplyMsgBase = data.writeReplyMsgBase;
				device.flushReplyMsgBase = data.flushReplyMsgBase;
				StorageManagerUtils::applyDefaultTransport(device);
				device.name = name;

				{
					const scoped_lock lock(storageMutex);
					device.id = StorageManagerUtils::allocateBlockDeviceIdLocked();
					blockDevices.push_back(device);
				}

				reply.success = true;
				reply.deviceId = device.id;
				registeredDevice = device;
				shouldProbe = true;

				printf("Storage: Registered block device %s id=%lu blocks=%lu blockSize=%u.", device.name.c_str(), device.id, device.blockCount, device.blockSize);
				fflush(stdout);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			const int replyResult = send_horizonos_message(storagePort, msg.src_port, &replyMsg);

			// Let the driver finish publishing the device before partition I/O is issued.
			if (replyResult == 0 and shouldProbe) {
				usleep(PARTITION_PROBE_SETTLE_DELAY_US);
				StorageManagerUtils::probeGpt(registeredDevice);
				StorageManagerUtils::probeMbr(registeredDevice);
				StorageManagerUtils::notifyFsHandlers(registeredDevice);
			}
		}
	}

	[[noreturn]] auto fsHandlerRegistrationHandler(void */*unused*/) -> void * {
		auto data = StorageRegisterFsHandlerMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_FS_HANDLER_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			string fsName;

			auto reply = StorageRegisterFsHandlerReplyMsgData();

			if (StorageManagerUtils::validName(data.fsName, data.fsNameLength, sizeof(data.fsName), fsName) and data.handlerPort != 0) {
				FsHandler handler {};

				handler.port = data.handlerPort;
				handler.tid = data.tid;
				handler.name = fsName;

				vector<BlockDevice> snapshot;

				{
					const scoped_lock lock(storageMutex);
					const bool exists = ranges::any_of(fsHandlers, [&](const FsHandler &curr) -> bool {
						return curr.name == fsName and curr.port == data.handlerPort;
					});

					if (!exists) {
						fsHandlers.push_back(handler);
					}

					snapshot = blockDevices;
				}

				reply.success = true;

				printf("Storage: Registered filesystem handler %s on port %lu.", fsName.c_str(), data.handlerPort);
				fflush(stdout);

				for (const auto &device : snapshot) {
					StorageManagerUtils::notifyFsHandlers(device);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_REGISTER_FS_HANDLER_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto blockUnregistrationHandler(void */*unused*/) -> void * {
		auto data = StorageUnregisterBlockDeviceMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_UNREGISTER_BLOCK_DEVICE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageUnregisterBlockDeviceReplyMsgData();

			{
				const scoped_lock lock(storageMutex);
				vector<uint64_t> removedParents;

				for (auto it = blockDevices.begin(); it != blockDevices.end();) {
					const bool matchById = data.deviceId != 0 and it->id == data.deviceId;
					const bool matchByDriver = data.deviceId == 0 and data.driverPort != 0 and it->driverPort == data.driverPort and it->controllerId == data.controllerId and it->nsid == data.nsid;

					if (it->kind == BlockDeviceKind::WholeDisk and (matchById or matchByDriver)) {
						removedParents.push_back(it->id);
						it = blockDevices.erase(it);
						++reply.removedCount;
						continue;
					}

					if (it->kind == BlockDeviceKind::Partition and (matchById or ranges::find(removedParents, it->parentId) != removedParents.end())) {
						it = blockDevices.erase(it);
						++reply.removedCount;
						continue;
					}

					++it;
				}
			}

			reply.success = reply.removedCount != 0;

			if (reply.success) {
				printf("Storage: Unregistered block device ctrl=%u nsid=%u removed=%u.", data.controllerId, data.nsid, reply.removedCount);
				fflush(stdout);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_UNREGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] auto readHandler(void */*unused*/) -> void * {
		auto data = StorageReadMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_READ_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageReadReplyMsgData();
			reply.requestId = data.requestId;
			reply.pageCount = data.pageCount;

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				BlockDevice blockDevice {};

				{
					const scoped_lock lock(storageMutex);
					const BlockDevice *device = StorageManagerUtils::findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						StorageManagerUtils::translateToBlockLocked(*device, data.lba, data.pageCount, blockDevice);
					}
				}

				if (blockDevice.driverPort != 0) {
					reply.success = StorageManagerUtils::blockRead(blockDevice, blockDevice.parentStartLba, data.pagePhysArray, data.pageCount);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_READ_REPLY_MSG_TYPE;
			const uint64_t replyPort = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.port = replyPort;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, replyPort, &replyMsg);
		}
	}

	[[noreturn]] auto writeHandler(void */*unused*/) -> void * {
		auto data = StorageWriteMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_WRITE_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageWriteReplyMsgData();
			reply.requestId = data.requestId;

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				BlockDevice blockDevice {};

				{
					const scoped_lock lock(storageMutex);
					const BlockDevice *device = StorageManagerUtils::findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						StorageManagerUtils::translateToBlockLocked(*device, data.lba, data.pageCount, blockDevice);
					}
				}

				if (blockDevice.driverPort != 0) {
					reply.success = StorageManagerUtils::blockWrite(blockDevice, blockDevice.parentStartLba, data.pagePhysArray, data.pageCount);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_WRITE_REPLY_MSG_TYPE;
			const uint64_t replyPort = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.port = replyPort;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, replyPort, &replyMsg);
		}
	}

	[[noreturn]] auto flushHandler(void */*unused*/) -> void * {
		auto data = StorageFlushMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_FLUSH_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageFlushReplyMsgData();
			reply.requestId = data.requestId;
			BlockDevice blockDevice {};

			{
				const scoped_lock lock(storageMutex);
				const BlockDevice *device = StorageManagerUtils::findBlockDeviceLocked(data.deviceId);

				if (device != nullptr) {
					if (device->kind == BlockDeviceKind::Partition) {
						const BlockDevice *parent = StorageManagerUtils::findBlockDeviceLocked(device->parentId);
						if (parent != nullptr) {
							blockDevice = *parent;
						}
					} else {
						blockDevice = *device;
					}
				}
			}

			if (blockDevice.driverPort != 0) {
				reply.success = StorageManagerUtils::blockFlush(blockDevice);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_FLUSH_REPLY_MSG_TYPE;
			const uint64_t replyPort = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.port = replyPort;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, replyPort, &replyMsg);
		}
	}

	[[noreturn]] auto listBlockDevicesHandler(void */*unused*/) -> void * {
		auto data = StorageListBlockDevicesMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_LIST_BLOCK_DEVICES_MSG_TYPE };
		filter.whiteListCount = 1;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(storagePort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = StorageListBlockDevicesReplyMsgData();
			reply.success = true;

			{
				const scoped_lock lock(storageMutex);
				reply.deviceCount = min<uint32_t>(blockDevices.size(), STORAGE_MAX_LIST_DEVICES);

				for (uint32_t i = 0; i < reply.deviceCount; ++i) {
					const BlockDevice &device = blockDevices[i];
					StorageListedBlockDevice &out = reply.devices[i];

					out.deviceId = device.id;
					out.kind = static_cast<uint8_t>(device.kind);
					out.blockCount = device.blockCount;
					out.blockSize = device.blockSize;
					out.parentId = device.parentId;
					out.parentStartLba = device.parentStartLba;
					StorageManagerUtils::fillName(out.name, sizeof(out.name), out.nameLength, device.name);
					StorageManagerUtils::fillName(out.label, sizeof(out.label), out.labelLength, device.label);
				}
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_LIST_BLOCK_DEVICES_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}
}

auto StorageManagerService::start() -> int {
	if (register_horizonos_port(reinterpret_cast<long *>(&storagePort)) != 0) {
		printf("Storage: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!StorageManagerUtils::registerWithNameRegistry("StorageManager")) {
		printf("Storage: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	const GetReplyMsgData nvmeInfo = StorageManagerUtils::waitForService("NVMe");
	nvmePort = nvmeInfo.port;

	if (register_horizonos_port(reinterpret_cast<long *>(&nvmeReplyPort)) != 0 or nvmeReplyPort == 0) {
		printf("Storage: Failed to register NVMe reply port.");
		fflush(stdout);

		return 1;
	}

	printf("Storage: Ready on port %lu, NVMe port %lu, NVMe reply port %lu.", storagePort, nvmePort, nvmeReplyPort);
	fflush(stdout);

	pthread_t blockThread;
	pthread_t unregisterThread;
	pthread_t fsThread;
	pthread_t readThread;
	pthread_t writeThread;
	pthread_t flushThread;
	pthread_t listThread;

	if (pthread_create(&blockThread, nullptr, blockRegistrationHandler, nullptr) != 0 or
	    pthread_create(&unregisterThread, nullptr, blockUnregistrationHandler, nullptr) != 0 or
	    pthread_create(&fsThread, nullptr, fsHandlerRegistrationHandler, nullptr) != 0 or
	    pthread_create(&readThread, nullptr, readHandler, nullptr) != 0 or
	    pthread_create(&writeThread, nullptr, writeHandler, nullptr) != 0 or
	    pthread_create(&flushThread, nullptr, flushHandler, nullptr) != 0 or
	    pthread_create(&listThread, nullptr, listBlockDevicesHandler, nullptr) != 0) {
		printf("Storage: Failed to create message handlers.");
		fflush(stdout);

		return 1;
	}

	pthread_detach(blockThread);
	pthread_detach(unregisterThread);
	pthread_detach(fsThread);
	pthread_detach(readThread);
	pthread_detach(writeThread);
	pthread_detach(flushThread);
	pthread_detach(listThread);

	for (;;) {
		usleep(100000);
	}
}

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int {
	StorageManagerService service;

	return service.start();
}
