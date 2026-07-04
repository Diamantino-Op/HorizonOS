#include "StorageProtocol.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

using namespace std;

namespace {
	enum class BlockDeviceKind : uint8_t {
		NvmeNamespace,
		Partition,
	};

	struct Guid {
		uint8_t bytes[16] {};
	};

	struct BlockDevice {
		uint64_t id {};
		BlockDeviceKind kind {};
		uint64_t driverPort {};
		uint32_t controllerId {};
		uint32_t nsid {};
		uint64_t blockCount {};
		uint32_t blockSize {};
		uint32_t maxPagesPerRequest {};
		uint64_t parentId {};
		uint64_t parentStartLba {};
		Guid partitionType {};
		Guid partitionId {};
		string name;
	};

	struct FsHandler {
		uint64_t port {};
		uint16_t tid {};
		string name;
	};

	struct GptHeader {
		uint64_t signature {};
		uint32_t revision {};
		uint32_t headerSize {};
		uint32_t headerCrc32 {};
		uint32_t reserved {};
		uint64_t currentLba {};
		uint64_t backupLba {};
		uint64_t firstUsableLba {};
		uint64_t lastUsableLba {};
		Guid diskGuid {};
		uint64_t partitionEntryLba {};
		uint32_t partitionEntryCount {};
		uint32_t partitionEntrySize {};
		uint32_t partitionEntryArrayCrc32 {};
	} __attribute__((packed));

	struct GptPartitionEntry {
		Guid partitionTypeGuid {};
		Guid uniquePartitionGuid {};
		uint64_t firstLba {};
		uint64_t lastLba {};
		uint64_t attributes {};
		uint16_t name[36] {};
	} __attribute__((packed));

	constexpr uint64_t GPT_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"

	uint64_t storagePort = 0;
	uint64_t nvmePort = 0;
	uint64_t nextBlockDeviceId = 1;
	uint64_t nvmeCpuId = 0;
	vector<BlockDevice> blockDevices;
	vector<FsHandler> fsHandlers;
	mutex storageMutex;

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
		const auto it = ranges::find_if(blockDevices, [&](const BlockDevice &dev) {
			return dev.id == id;
		});

		return it == blockDevices.end() ? nullptr : &(*it);
	}

	auto transferBlockCount(const BlockDevice &device, const uint32_t pageCount) -> uint64_t {
		return (static_cast<uint64_t>(pageCount) * 0x1000) / device.blockSize;
	}

	auto translateToNvmeLocked(const BlockDevice &device, uint64_t lba, const uint32_t pageCount, BlockDevice &out) -> bool {
		const uint64_t blocks = transferBlockCount(device, pageCount);

		if (blocks == 0 or lba >= device.blockCount or blocks > device.blockCount - lba) {
			return false;
		}

		if (device.kind == BlockDeviceKind::NvmeNamespace) {
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

	auto nvmeRead(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		auto data = NvmeReadMsgData();
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;
		data.lba = lba;
		data.pageCount = pageCount;
		memcpy(data.pagePhysArray, pagePhysArray, pageCount * sizeof(uint64_t));

		auto msg = hos_msg();
		msg.type = NVME_READ_MSG_BASE + nvmeCpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storagePort, device.driverPort, &msg) != 0) {
			return false;
		}

		auto reply = NvmeReadReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { NVME_REPLY_READ_MSG_BASE + nvmeCpuId };
		filter.whiteListCount = 1;
		const int ret = receive_horizonos_message(storagePort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto nvmeWrite(const BlockDevice &device, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
		auto data = NvmeWriteMsgData();
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;
		data.lba = lba;
		data.pageCount = pageCount;
		memcpy(data.pagePhysArray, pagePhysArray, pageCount * sizeof(uint64_t));

		auto msg = hos_msg();
		msg.type = NVME_WRITE_MSG_BASE + nvmeCpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storagePort, device.driverPort, &msg) != 0) {
			return false;
		}

		auto reply = NvmeWriteReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { NVME_REPLY_WRITE_MSG_BASE + nvmeCpuId };
		filter.whiteListCount = 1;
		const int ret = receive_horizonos_message(storagePort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto nvmeFlush(const BlockDevice &device) -> bool {
		auto data = NvmeFlushMsgData();
		data.controllerId = device.controllerId;
		data.nsid = device.nsid;

		auto msg = hos_msg();
		msg.type = NVME_FLUSH_MSG_BASE + nvmeCpuId;
		msg.port = device.driverPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storagePort, device.driverPort, &msg) != 0) {
			return false;
		}

		auto reply = NvmeFlushReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { NVME_REPLY_FLUSH_MSG_BASE + nvmeCpuId };
		filter.whiteListCount = 1;
		const int ret = receive_horizonos_message(storagePort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
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

		if (!nvmeRead(device, lba, pages, 1)) {
			munmap(reinterpret_cast<void *>(virt), 0x1000);
			freePhysPage(phys);
			phys = 0;
			virt = 0;
			return false;
		}

		return true;
	}

	void freeOnePage(uint64_t phys, uint64_t virt) {
		if (virt != 0) {
			munmap(reinterpret_cast<void *>(virt), 0x1000);
		}

		if (phys != 0) {
			freePhysPage(phys);
		}
	}

	void notifyFsHandlers(const BlockDevice &device) {
		vector<FsHandler> handlers;

		{
			scoped_lock lock(storageMutex);
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
		uint32_t created = 0;

		for (uint32_t i = 0; i < maxEntries; ++i) {
			const uint64_t entryPageLba = header->partitionEntryLba + (i / entriesPerPage) * (0x1000 / rawDevice.blockSize);
			const uint32_t entryIndexInPage = i % entriesPerPage;

			uint64_t entriesPhys = 0;
			uint64_t entriesVirt = 0;

			if (!readOnePage(rawDevice, entryPageLba, entriesPhys, entriesVirt)) {
				continue;
			}

			const auto *entry = reinterpret_cast<const GptPartitionEntry *>(entriesVirt + (entryIndexInPage * header->partitionEntrySize));
			const bool empty = ranges::all_of(entry->partitionTypeGuid.bytes, [](const uint8_t byte) {
				return byte == 0;
			});

			if (!empty and entry->firstLba <= entry->lastLba and entry->lastLba < rawDevice.blockCount) {
				BlockDevice partition {};
				partition.id = nextBlockDeviceId++;
				partition.kind = BlockDeviceKind::Partition;
				partition.driverPort = rawDevice.driverPort;
				partition.controllerId = rawDevice.controllerId;
				partition.nsid = rawDevice.nsid;
				partition.blockCount = entry->lastLba - entry->firstLba + 1;
				partition.blockSize = rawDevice.blockSize;
				partition.maxPagesPerRequest = rawDevice.maxPagesPerRequest;
				partition.parentId = rawDevice.id;
				partition.parentStartLba = entry->firstLba;
				partition.partitionType = entry->partitionTypeGuid;
				partition.partitionId = entry->uniquePartitionGuid;
				partition.name = rawDevice.name + "p" + to_string(created + 1);

				{
					scoped_lock lock(storageMutex);
					blockDevices.push_back(partition);
				}

				printf("Storage: Registered partition %s id=%lu start=%lu blocks=%lu.", partition.name.c_str(), partition.id, partition.parentStartLba, partition.blockCount);
				fflush(stdout);
				notifyFsHandlers(partition);
				++created;
			}

			freeOnePage(entriesPhys, entriesVirt);
		}

		freeOnePage(headerPhys, headerVirt);

		printf("Storage: GPT probe for %s created %u partition(s).", rawDevice.name.c_str(), created);
		fflush(stdout);
	}

	[[noreturn]] void *blockRegistrationHandler(void *) {
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
				device.id = nextBlockDeviceId++;
				device.kind = BlockDeviceKind::NvmeNamespace;
				device.driverPort = data.driverPort;
				device.controllerId = data.controllerId;
				device.nsid = data.nsid;
				device.blockCount = data.blockCount;
				device.blockSize = data.blockSize;
				device.maxPagesPerRequest = min<uint32_t>(data.maxPagesPerRequest, STORAGE_MAX_PAGES_PER_MSG);
				device.name = name;

				{
					scoped_lock lock(storageMutex);
					blockDevices.push_back(device);
				}

				reply.success = true;
				reply.deviceId = device.id;

				printf("Storage: Registered block device %s id=%lu blocks=%lu blockSize=%u.", device.name.c_str(), device.id, device.blockCount, device.blockSize);
				fflush(stdout);
				notifyFsHandlers(device);
				probeGpt(device);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] void *fsHandlerRegistrationHandler(void *) {
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
					scoped_lock lock(storageMutex);
					const bool exists = ranges::any_of(fsHandlers, [&](const FsHandler &curr) {
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

	[[noreturn]] void *readHandler(void *) {
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
			reply.pageCount = data.pageCount;

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				BlockDevice nvmeDevice {};

				{
					scoped_lock lock(storageMutex);
					const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						translateToNvmeLocked(*device, data.lba, data.pageCount, nvmeDevice);
					}
				}

				if (nvmeDevice.driverPort != 0) {
					reply.success = nvmeRead(nvmeDevice, nvmeDevice.parentStartLba, data.pagePhysArray, data.pageCount);
				}
			}

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_READ_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] void *writeHandler(void *) {
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

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				BlockDevice nvmeDevice {};

				{
					scoped_lock lock(storageMutex);
					const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

					if (device != nullptr) {
						translateToNvmeLocked(*device, data.lba, data.pageCount, nvmeDevice);
					}
				}

				if (nvmeDevice.driverPort != 0) {
					reply.success = nvmeWrite(nvmeDevice, nvmeDevice.parentStartLba, data.pagePhysArray, data.pageCount);
				}
			}

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_WRITE_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}

	[[noreturn]] void *flushHandler(void *) {
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
			BlockDevice nvmeDevice {};

			{
				scoped_lock lock(storageMutex);
				const BlockDevice *device = findBlockDeviceLocked(data.deviceId);

				if (device != nullptr) {
					if (device->kind == BlockDeviceKind::Partition) {
						const BlockDevice *parent = findBlockDeviceLocked(device->parentId);
						if (parent != nullptr) {
							nvmeDevice = *parent;
						}
					} else {
						nvmeDevice = *device;
					}
				}
			}

			if (nvmeDevice.driverPort != 0) {
				reply.success = nvmeFlush(nvmeDevice);
			}

			auto replyMsg = hos_msg();
			replyMsg.type = STORAGE_FLUSH_REPLY_MSG_TYPE;
			replyMsg.port = msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);
			send_horizonos_message(storagePort, msg.src_port, &replyMsg);
		}
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
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

	long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpuCount > 0) {
		auto *cpuIds = new HosCpuInfo[cpuCount];
		if (getCpuIds(cpuIds, static_cast<uint64_t>(cpuCount)) == 0) {
			nvmeCpuId = cpuIds[0].cpuId;
		}
		delete[] cpuIds;
	}

	printf("Storage: Ready on port %lu, NVMe port %lu, NVMe CPU message slot %lu.", storagePort, nvmePort, nvmeCpuId);
	fflush(stdout);

	pthread_t blockThread;
	pthread_t fsThread;
	pthread_t readThread;
	pthread_t writeThread;
	pthread_t flushThread;

	if (pthread_create(&blockThread, nullptr, blockRegistrationHandler, nullptr) != 0 or
	    pthread_create(&fsThread, nullptr, fsHandlerRegistrationHandler, nullptr) != 0 or
	    pthread_create(&readThread, nullptr, readHandler, nullptr) != 0 or
	    pthread_create(&writeThread, nullptr, writeHandler, nullptr) != 0 or
	    pthread_create(&flushThread, nullptr, flushHandler, nullptr) != 0) {
		printf("Storage: Failed to create message handlers.");
		fflush(stdout);
		return 1;
	}

	pthread_detach(blockThread);
	pthread_detach(fsThread);
	pthread_detach(readThread);
	pthread_detach(writeThread);
	pthread_detach(flushThread);

	for (;;) {}
}
