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

constexpr uint64_t FS_MOUNT_MSG_TYPE       = 0x80000;
constexpr uint64_t FS_MOUNT_REPLY_MSG_TYPE = 0x80001;
constexpr uint64_t FS_STAT_MSG_TYPE        = 0x80002;
constexpr uint64_t FS_STAT_REPLY_MSG_TYPE  = 0x80003;
constexpr uint64_t FS_READDIR_MSG_TYPE     = 0x80004;
constexpr uint64_t FS_READDIR_REPLY_MSG_TYPE = 0x80005;
constexpr uint64_t FS_READ_MSG_TYPE        = 0x80006;
constexpr uint64_t FS_READ_REPLY_MSG_TYPE  = 0x80007;

constexpr uint64_t VFS_STAT_MSG_TYPE       = 0x90000;
constexpr uint64_t VFS_STAT_REPLY_MSG_TYPE = 0x90001;
constexpr uint64_t VFS_READDIR_MSG_TYPE    = 0x90002;
constexpr uint64_t VFS_READDIR_REPLY_MSG_TYPE = 0x90003;
constexpr uint64_t VFS_READ_MSG_TYPE       = 0x90004;
constexpr uint64_t VFS_READ_REPLY_MSG_TYPE = 0x90005;

constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t STORAGE_MAX_LIST_DEVICES = 64;
constexpr uint32_t VFS_MAX_PATH_LENGTH = 256;
constexpr uint32_t VFS_MAX_NAME_LENGTH = 64;
constexpr uint32_t VFS_MAX_READ_SIZE = 2048;
constexpr uint32_t VFS_MAX_DIR_ENTRIES = 32;

constexpr uint8_t VFS_NODE_UNKNOWN = 0;
constexpr uint8_t VFS_NODE_FILE = 1;
constexpr uint8_t VFS_NODE_DIRECTORY = 2;

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

struct FsMountMsgData {
	uint64_t deviceId {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	char deviceName[STORAGE_MAX_NAME_LENGTH] {};
	size_t deviceNameLength {};
};

struct FsMountReplyMsgData {
	bool success {};
	uint64_t mountId {};
};

struct FsStatMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsStatReplyMsgData {
	bool success {};
	uint8_t nodeType {};
	uint64_t size {};
};

struct FsReadDirMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsDirEntry {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint8_t nodeType {};
	uint64_t size {};
};

struct FsReadDirReplyMsgData {
	bool success {};
	uint32_t entryCount {};
	VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {};
};

struct FsReadMsgData {
	uint64_t mountId {};
	uint64_t offset {};
	uint32_t length {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsReadReplyMsgData {
	bool success {};
	uint32_t bytesRead {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

struct VfsStatMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsStatReplyMsgData {
	bool success {};
	uint8_t nodeType {};
	uint64_t size {};
};

struct VfsReadDirMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsReadDirReplyMsgData {
	bool success {};
	uint32_t entryCount {};
	VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {};
};

struct VfsReadMsgData {
	uint64_t offset {};
	uint32_t length {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsReadReplyMsgData {
	bool success {};
	uint32_t bytesRead {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

#endif
