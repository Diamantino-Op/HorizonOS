#ifndef HORIZONOS_VFS_PROTOCOL_HPP
#define HORIZONOS_VFS_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t STORAGE_LIST_BLOCK_DEVICES_MSG_TYPE       = 0x7000C;
constexpr uint64_t STORAGE_LIST_BLOCK_DEVICES_REPLY_MSG_TYPE = 0x7000D;

constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t STORAGE_MAX_LIST_DEVICES = 64;

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
