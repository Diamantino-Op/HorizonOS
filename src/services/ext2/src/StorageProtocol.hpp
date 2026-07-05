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

constexpr uint64_t FS_MOUNT_MSG_TYPE       = 0x80000;
constexpr uint64_t FS_MOUNT_REPLY_MSG_TYPE = 0x80001;
constexpr uint64_t FS_STAT_MSG_TYPE        = 0x80002;
constexpr uint64_t FS_STAT_REPLY_MSG_TYPE  = 0x80003;
constexpr uint64_t FS_READDIR_MSG_TYPE     = 0x80004;
constexpr uint64_t FS_READDIR_REPLY_MSG_TYPE = 0x80005;
constexpr uint64_t FS_READ_MSG_TYPE        = 0x80006;
constexpr uint64_t FS_READ_REPLY_MSG_TYPE  = 0x80007;
constexpr uint64_t FS_WRITE_MSG_TYPE       = 0x80008;
constexpr uint64_t FS_WRITE_REPLY_MSG_TYPE = 0x80009;
constexpr uint64_t FS_CREATE_MSG_TYPE      = 0x8000A;
constexpr uint64_t FS_CREATE_REPLY_MSG_TYPE = 0x8000B;
constexpr uint64_t FS_UNLINK_MSG_TYPE      = 0x8000C;
constexpr uint64_t FS_UNLINK_REPLY_MSG_TYPE = 0x8000D;
constexpr uint64_t FS_RENAME_MSG_TYPE      = 0x8000E;
constexpr uint64_t FS_RENAME_REPLY_MSG_TYPE = 0x8000F;
constexpr uint64_t FS_TRUNCATE_MSG_TYPE    = 0x80010;
constexpr uint64_t FS_TRUNCATE_REPLY_MSG_TYPE = 0x80011;
constexpr uint64_t FS_MKDIR_MSG_TYPE       = 0x80012;
constexpr uint64_t FS_MKDIR_REPLY_MSG_TYPE = 0x80013;

constexpr uint32_t STORAGE_MAX_PAGES_PER_MSG = 256;
constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t VFS_MAX_PATH_LENGTH = 256;
constexpr uint32_t VFS_MAX_NAME_LENGTH = 64;
constexpr uint32_t VFS_MAX_READ_SIZE = 2048;
constexpr uint32_t VFS_MAX_DIR_ENTRIES = 32;

constexpr uint8_t VFS_NODE_UNKNOWN = 0;
constexpr uint8_t VFS_NODE_FILE = 1;
constexpr uint8_t VFS_NODE_DIRECTORY = 2;

constexpr uint32_t VFS_STATUS_OK = 0;
constexpr uint32_t VFS_STATUS_NOT_FOUND = 2;
constexpr uint32_t VFS_STATUS_BUSY = 16;
constexpr uint32_t VFS_STATUS_EXISTS = 17;
constexpr uint32_t VFS_STATUS_NOT_DIR = 20;
constexpr uint32_t VFS_STATUS_IS_DIR = 21;
constexpr uint32_t VFS_STATUS_INVALID = 22;
constexpr uint32_t VFS_STATUS_NO_SPACE = 28;
constexpr uint32_t VFS_STATUS_READ_ONLY = 30;
constexpr uint32_t VFS_STATUS_NOT_EMPTY = 39;
constexpr uint32_t VFS_STATUS_UNSUPPORTED = 95;

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
	uint64_t deviceId {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct StorageReadReplyMsgData {
	bool success {};
	uint32_t pageCount {};
};

struct StorageWriteMsgData {
	uint64_t deviceId {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct StorageWriteReplyMsgData {
	bool success {};
};

struct StorageFlushMsgData {
	uint64_t deviceId {};
};

struct StorageFlushReplyMsgData {
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
	uint32_t status {};
	uint64_t nodeId {};
};

struct FsReadDirMsgData {
	uint64_t mountId {};
	uint32_t offset {};
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
	uint32_t status {};
	uint32_t nextOffset {};
	bool hasMore {};
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

struct FsWriteMsgData {
	uint64_t mountId {};
	uint64_t offset {};
	uint32_t length {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsWriteReplyMsgData {
	bool success {};
	uint32_t bytesWritten {};
	uint64_t size {};
	uint32_t status {};
};

struct FsCreateMsgData {
	uint64_t mountId {};
	uint8_t nodeType {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsCreateReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct FsUnlinkMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsUnlinkReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct FsRenameMsgData {
	uint64_t mountId {};
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct FsRenameReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct FsTruncateMsgData {
	uint64_t mountId {};
	uint64_t size {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsTruncateReplyMsgData {
	bool success {};
	uint64_t size {};
	uint32_t status {};
};

struct FsMkdirMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsMkdirReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

#endif
