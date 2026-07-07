#include "StorageProtocol.hpp"
#include "StorageManager.hpp"

#include "bits/linux/linux_sched.h"
#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

namespace {
	constexpr uint64_t GPT_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"

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

	auto allocateBlockDeviceIdLocked() -> uint64_t {
		return nextBlockDeviceId++;
	}

	auto allocateNvmeRequestId() -> uint64_t {
		const scoped_lock lock(nvmeRequestIdMutex);
		return nextNvmeRequestId++;
	}

	auto validName(const char *name, const size_t length, const size_t maxLength, string &out) -> bool {
		if (length == 0 or length > maxLength or name[length - 1] != '\0') {
			return false;
		}

		out.assign(name, length - 1);

		return true;
	}

	void fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());

		memcpy(dst, name.data(), copyLen);

		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto gptNameToString(const uint16_t *name, const size_t charCount) -> string {
		string out;

		for (size_t i = 0; i < charCount and name[i] != 0; ++i) {
			const uint16_t ch = name[i];

			out.push_back(ch >= 0x20 and ch <= 0x7E ? static_cast<char>(ch) : '_');
		}

		return out;
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

	auto waitForService(const char *name) -> GetReplyMsgData {
		for (;;) {
			auto check = CheckMsgData();

			fillName(check.name, sizeof(check.name), check.nameLength, name);

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

		fillName(get.name, sizeof(get.name), get.nameLength, name);

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

	auto findBlockDeviceLocked(const uint64_t id) -> BlockDevice * {
		const auto it = ranges::find_if(blockDevices, [&](const BlockDevice &dev) -> bool {
			return dev.id == id;
		});

		return it == blockDevices.end() ? nullptr : &(*it);
	}

	auto transferBlockCount(const BlockDevice &device, const uint32_t pageCount) -> uint64_t {
		return (static_cast<uint64_t>(pageCount) * 0x1000) / device.blockSize;
	}

	auto translateToBlockLocked(const BlockDevice &device, const uint64_t lba, const uint32_t pageCount, BlockDevice &out) -> bool {
		const uint64_t blocks = transferBlockCount(device, pageCount);

		if (blocks == 0 or lba >= device.blockCount or blocks > device.blockCount - lba) {
			return false;
		}

		if (device.kind == BlockDeviceKind::WholeDisk) {
			out = device;
			out.parentStartLba = lba;

			return true;
		}

		const BlockDevice *parent = findBlockDeviceLocked(device.parentId);

		if (parent == nullptr or device.parentStartLba + lba >= parent->blockCount or blocks > parent->blockCount - (device.parentStartLba + lba)) {
			return false;
		}

		out = *parent;
		out.parentStartLba = device.parentStartLba + lba;

		return true;
	}

	auto currentCpuId() -> uint64_t {
		const int cpuId = sched_getcpu();

		return cpuId < 0 ? 0 : static_cast<uint64_t>(cpuId);
	}

	void applyDefaultTransport(BlockDevice &device) {
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

	auto blockRead(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = currentCpuId();
		const uint64_t requestId = allocateNvmeRequestId();

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
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { device.readReplyMsgBase + cpuId };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(nvmeReplyPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		//printf("Storage: NVMe read reply for %s ret=%d success=%d.", device.name.c_str(), ret, reply.success);
		//fflush(stdout);

		return ret == 0 and reply.requestId == requestId and reply.success;
	}

	auto blockWrite(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = currentCpuId();
		const uint64_t requestId = allocateNvmeRequestId();

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
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { device.writeReplyMsgBase + cpuId };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(nvmeReplyPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.requestId == requestId and reply.success;
	}

	auto blockFlush(const BlockDevice &device) -> bool {
		const scoped_lock requestLock(blockRequestMutex);
		const uint64_t cpuId = currentCpuId();
		const uint64_t requestId = allocateNvmeRequestId();

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
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { device.flushReplyMsgBase + cpuId };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(nvmeReplyPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.requestId == requestId and reply.success;
	}

	auto readOnePage(const BlockDevice &device, const uint64_t lba, uint64_t &phys, uint64_t &virt) -> bool {
		if (allocPhysPage(&phys) != 0) {
			return false;
		}

		if (mmap_phys(phys, 0x1000, &virt, false) != 0) {
			freePhysPage(phys);

			phys = 0;

			return false;
		}

		memset(reinterpret_cast<void *>(virt), 0, 0x1000);
		const uint64_t pages[1] { phys };

		if (!blockRead(device, lba, pages, 1)) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
			freePhysPage(phys);

			phys = 0;
			virt = 0;

			return false;
		}

		return true;
	}

	void freeOnePage(const uint64_t phys, const uint64_t virt) {
		if (virt != 0) {
			munmap_extra(reinterpret_cast<void *>(virt), 0x1000, false);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	void notifyFsHandlers(const BlockDevice &device) {
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
			fillName(data.deviceName, sizeof(data.deviceName), data.deviceNameLength, device.name);

			auto msg = hos_msg();
			msg.type = STORAGE_FS_PROBE_DEVICE_MSG_TYPE;
			msg.port = handler.port;
			msg.buffer = &data;
			msg.length = sizeof(data);

			send_horizonos_message(storagePort, handler.port, &msg);
		}
	}

	void probeGpt(const BlockDevice &rawDevice) {
		uint64_t headerPhys = 0;
		uint64_t headerVirt = 0;

		printf("Storage: Probing GPT on %s.", rawDevice.name.c_str());
		fflush(stdout);

		if (!readOnePage(rawDevice, 1, headerPhys, headerVirt)) {
			printf("Storage: Failed to read GPT header from %s.", rawDevice.name.c_str());
			fflush(stdout);

			return;
		}

		const auto *header = reinterpret_cast<const GptHeader *>(headerVirt);

		if (header->signature != GPT_SIGNATURE or header->headerSize < sizeof(GptHeader) or header->partitionEntrySize < sizeof(GptPartitionEntry)) {
			freeOnePage(headerPhys, headerVirt);

			printf("Storage: %s has no GPT header.", rawDevice.name.c_str());
			fflush(stdout);

			return;
		}

		const uint32_t entriesPerPage = 0x1000 / header->partitionEntrySize;
		const uint32_t maxEntries = min<uint32_t>(header->partitionEntryCount, 128);
		const uint32_t entryPageCount = (maxEntries + entriesPerPage - 1) / entriesPerPage;
		uint32_t created = 0;

		for (uint32_t pageIndex = 0; pageIndex < entryPageCount; ++pageIndex) {
			const uint64_t entryPageLba = header->partitionEntryLba + (static_cast<uint64_t>(pageIndex * (0x1000 / rawDevice.blockSize)));
			uint64_t entriesPhys = 0;
			uint64_t entriesVirt = 0;

			if (!readOnePage(rawDevice, entryPageLba, entriesPhys, entriesVirt)) {
				continue;
			}

			const uint32_t firstEntry = pageIndex * entriesPerPage;
			const uint32_t pageEntryCount = min<uint32_t>(entriesPerPage, maxEntries - firstEntry);

			for (uint32_t entryIndex = 0; entryIndex < pageEntryCount; ++entryIndex) {
				const uint32_t globalEntryIndex = firstEntry + entryIndex;
				const auto *entry = reinterpret_cast<const GptPartitionEntry *>(entriesVirt + (static_cast<uint64_t>(entryIndex * header->partitionEntrySize)));
				const bool empty = ranges::all_of(entry->partitionTypeGuid.bytes, [](const uint8_t byte) -> bool {
					return byte == 0;
				});

				if (!empty and entry->firstLba <= entry->lastLba and entry->lastLba < rawDevice.blockCount) {
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
					partition.label = gptNameToString(entry->name, 36);

					{
						const scoped_lock lock(storageMutex);
						partition.id = allocateBlockDeviceIdLocked();
						blockDevices.push_back(partition);
					}

					printf("Storage: Registered partition %s id=%lu start=%lu blocks=%lu.", partition.name.c_str(), partition.id, partition.parentStartLba, partition.blockCount);
					fflush(stdout);

					notifyFsHandlers(partition);
					++created;
				}
			}

			freeOnePage(entriesPhys, entriesVirt);
		}

		freeOnePage(headerPhys, headerVirt);

		printf("Storage: GPT probe for %s created %u partition(s).", rawDevice.name.c_str(), created);
		fflush(stdout);
	}

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

			if (validName(data.name, data.nameLength, sizeof(data.name), name) and data.blockSize != 0 and data.blockCount != 0) {
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
				applyDefaultTransport(device);
				device.name = name;

				{
					const scoped_lock lock(storageMutex);
					device.id = allocateBlockDeviceIdLocked();
					blockDevices.push_back(device);
				}

				reply.success = true;
				reply.deviceId = device.id;

				printf("Storage: Registered block device %s id=%lu blocks=%lu blockSize=%u.", device.name.c_str(), device.id, device.blockCount, device.blockSize);
				fflush(stdout);

				probeGpt(device);
				notifyFsHandlers(device);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
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

			if (validName(data.fsName, data.fsNameLength, sizeof(data.fsName), fsName) and data.handlerPort != 0) {
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
					notifyFsHandlers(device);
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
					const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						translateToBlockLocked(*device, data.lba, data.pageCount, blockDevice);
					}
				}

				if (blockDevice.driverPort != 0) {
					reply.success = blockRead(blockDevice, blockDevice.parentStartLba, data.pagePhysArray, data.pageCount);
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
					const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						translateToBlockLocked(*device, data.lba, data.pageCount, blockDevice);
					}
				}

				if (blockDevice.driverPort != 0) {
					reply.success = blockWrite(blockDevice, blockDevice.parentStartLba, data.pagePhysArray, data.pageCount);
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
				const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

				if (device != nullptr) {
					if (device->kind == BlockDeviceKind::Partition) {
						const BlockDevice *parent = findBlockDeviceLocked(device->parentId);
						if (parent != nullptr) {
							blockDevice = *parent;
						}
					} else {
						blockDevice = *device;
					}
				}
			}

			if (blockDevice.driverPort != 0) {
				reply.success = blockFlush(blockDevice);
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
					fillName(out.name, sizeof(out.name), out.nameLength, device.name);
					fillName(out.label, sizeof(out.label), out.labelLength, device.label);
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

auto main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) -> int {
	if (register_horizonos_port(reinterpret_cast<long *>(&storagePort)) != 0) {
		printf("Storage: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	if (!registerWithNameRegistry("StorageManager")) {
		printf("Storage: Failed to register with Name/Registry.");
		fflush(stdout);

		return 1;
	}

	const GetReplyMsgData nvmeInfo = waitForService("NVMe");
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
