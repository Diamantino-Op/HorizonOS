#ifndef HORIZONOS_STORAGE_PROTOCOL_HPP
#define HORIZONOS_STORAGE_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t NVME_READ_MSG_BASE        = 0x10000;
constexpr uint64_t NVME_WRITE_MSG_BASE       = 0x20000;
constexpr uint64_t NVME_FLUSH_MSG_BASE       = 0x30000;
constexpr uint64_t NVME_REPLY_READ_MSG_BASE  = 0x40000;
constexpr uint64_t NVME_REPLY_WRITE_MSG_BASE = 0x50000;
constexpr uint64_t NVME_REPLY_FLUSH_MSG_BASE = 0x60000;

constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_MSG_TYPE       = 0x70000;
constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE = 0x70001;
constexpr uint64_t STORAGE_REGISTER_FS_HANDLER_MSG_TYPE         = 0x70002;
constexpr uint64_t STORAGE_REGISTER_FS_HANDLER_REPLY_MSG_TYPE   = 0x70003;
constexpr uint64_t STORAGE_READ_MSG_TYPE                        = 0x70004;
constexpr uint64_t STORAGE_READ_REPLY_MSG_TYPE                  = 0x70005;
constexpr uint64_t STORAGE_WRITE_MSG_TYPE                       = 0x70006;
constexpr uint64_t STORAGE_WRITE_REPLY_MSG_TYPE                 = 0x70007;
constexpr uint64_t STORAGE_FLUSH_MSG_TYPE                       = 0x70008;
constexpr uint64_t STORAGE_FLUSH_REPLY_MSG_TYPE                 = 0x70009;
constexpr uint64_t STORAGE_FS_PROBE_DEVICE_MSG_TYPE             = 0x7000A;
constexpr uint64_t STORAGE_FS_PROBE_DEVICE_REPLY_MSG_TYPE       = 0x7000B;
constexpr uint64_t STORAGE_LIST_BLOCK_DEVICES_MSG_TYPE          = 0x7000C;
constexpr uint64_t STORAGE_LIST_BLOCK_DEVICES_REPLY_MSG_TYPE    = 0x7000D;
constexpr uint64_t STORAGE_UNREGISTER_BLOCK_DEVICE_MSG_TYPE       = 0x7000E;
constexpr uint64_t STORAGE_UNREGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE = 0x7000F;

constexpr uint32_t STORAGE_MAX_PAGES_PER_MSG = 256;
constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t STORAGE_MAX_LIST_DEVICES = 64;

constexpr uint8_t STORAGE_TRANSPORT_NVME_COMPAT = 0;
constexpr uint8_t STORAGE_TRANSPORT_GENERIC_BLOCK = 1;

struct RegisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct GetMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct CheckMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct RegisterReplyMsgData {
	bool success {};
};

struct CheckReplyMsgData {
	bool exists {};
};

struct GetReplyMsgData {
	uint64_t port {};
	uint16_t tid {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct NvmeReadMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct NvmeReadReplyMsgData {
	uint64_t requestId {};
	bool success {};
	uint32_t pageCount {};
};

struct NvmeWriteMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct NvmeWriteReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct NvmeFlushMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
};

struct NvmeFlushReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct StorageRegisterBlockDeviceMsgData {
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	uint32_t maxPagesPerRequest {};
	char name[STORAGE_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint8_t transport {};
	uint64_t readMsgBase {};
	uint64_t writeMsgBase {};
	uint64_t flushMsgBase {};
	uint64_t readReplyMsgBase {};
	uint64_t writeReplyMsgBase {};
	uint64_t flushReplyMsgBase {};
};

struct StorageRegisterBlockDeviceReplyMsgData {
	bool success {};
	uint64_t deviceId {};
};

struct StorageUnregisterBlockDeviceMsgData {
	uint64_t deviceId {};
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
};

struct StorageUnregisterBlockDeviceReplyMsgData {
	bool success {};
	uint32_t removedCount {};
};

struct StorageRegisterFsHandlerMsgData {
	uint64_t handlerPort {};
	uint16_t ownerPid {};
	uint16_t tid {};
	char fsName[16] {};
	size_t fsNameLength {};
};

struct StorageRegisterFsHandlerReplyMsgData {
	bool success {};
};

struct StorageReadMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint64_t deviceId {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct StorageReadReplyMsgData {
	uint64_t requestId {};
	bool success {};
	uint32_t pageCount {};
};

struct StorageWriteMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint64_t deviceId {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct StorageWriteReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct StorageFlushMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint64_t deviceId {};
};

struct StorageFlushReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct StorageFsProbeDeviceMsgData {
	uint64_t deviceId {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	char deviceName[STORAGE_MAX_NAME_LENGTH] {};
	size_t deviceNameLength {};
};

struct StorageFsProbeDeviceReplyMsgData {
	bool recognized {};
};

struct StorageListBlockDevicesMsgData {
	uint32_t reserved {};
};

struct StorageListedBlockDevice {
	uint64_t deviceId {};
	uint8_t kind {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	uint64_t parentId {};
	uint64_t parentStartLba {};
	char name[STORAGE_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	char label[STORAGE_MAX_NAME_LENGTH] {};
	size_t labelLength {};
};

struct StorageListBlockDevicesReplyMsgData {
	bool success {};
	uint32_t deviceCount {};
	StorageListedBlockDevice devices[STORAGE_MAX_LIST_DEVICES] {};
};

#endif
