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

constexpr uint64_t VFS_STAT_MSG_TYPE       = 0x90000;
constexpr uint64_t VFS_STAT_REPLY_MSG_TYPE = 0x90001;
constexpr uint64_t VFS_READDIR_MSG_TYPE    = 0x90002;
constexpr uint64_t VFS_READDIR_REPLY_MSG_TYPE = 0x90003;
constexpr uint64_t VFS_READ_MSG_TYPE       = 0x90004;
constexpr uint64_t VFS_READ_REPLY_MSG_TYPE = 0x90005;
constexpr uint64_t VFS_WRITE_MSG_TYPE      = 0x90006;
constexpr uint64_t VFS_WRITE_REPLY_MSG_TYPE = 0x90007;
constexpr uint64_t VFS_CREATE_MSG_TYPE     = 0x90008;
constexpr uint64_t VFS_CREATE_REPLY_MSG_TYPE = 0x90009;
constexpr uint64_t VFS_OPEN_MSG_TYPE       = 0x9000A;
constexpr uint64_t VFS_OPEN_REPLY_MSG_TYPE = 0x9000B;
constexpr uint64_t VFS_CLOSE_MSG_TYPE      = 0x9000C;
constexpr uint64_t VFS_CLOSE_REPLY_MSG_TYPE = 0x9000D;
constexpr uint64_t VFS_HANDLE_READ_MSG_TYPE = 0x9000E;
constexpr uint64_t VFS_HANDLE_READ_REPLY_MSG_TYPE = 0x9000F;
constexpr uint64_t VFS_HANDLE_WRITE_MSG_TYPE = 0x90010;
constexpr uint64_t VFS_HANDLE_WRITE_REPLY_MSG_TYPE = 0x90011;
constexpr uint64_t VFS_UNLINK_MSG_TYPE     = 0x90012;
constexpr uint64_t VFS_UNLINK_REPLY_MSG_TYPE = 0x90013;
constexpr uint64_t VFS_RENAME_MSG_TYPE     = 0x90014;
constexpr uint64_t VFS_RENAME_REPLY_MSG_TYPE = 0x90015;
constexpr uint64_t VFS_TRUNCATE_MSG_TYPE   = 0x90016;
constexpr uint64_t VFS_TRUNCATE_REPLY_MSG_TYPE = 0x90017;
constexpr uint64_t VFS_HANDLE_SEEK_MSG_TYPE = 0x90018;
constexpr uint64_t VFS_HANDLE_SEEK_REPLY_MSG_TYPE = 0x90019;

constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t STORAGE_MAX_LIST_DEVICES = 64;
constexpr uint32_t VFS_MAX_PATH_LENGTH = 256;
constexpr uint32_t VFS_MAX_NAME_LENGTH = 64;
constexpr uint32_t VFS_MAX_READ_SIZE = 2048;
constexpr uint32_t VFS_MAX_DIR_ENTRIES = 32;

constexpr uint8_t VFS_NODE_UNKNOWN = 0;
constexpr uint8_t VFS_NODE_FILE = 1;
constexpr uint8_t VFS_NODE_DIRECTORY = 2;

constexpr uint32_t VFS_OPEN_READ = 1 << 0;
constexpr uint32_t VFS_OPEN_WRITE = 1 << 1;
constexpr uint32_t VFS_OPEN_CREATE = 1 << 2;

constexpr uint8_t VFS_SEEK_SET = 0;
constexpr uint8_t VFS_SEEK_CUR = 1;
constexpr uint8_t VFS_SEEK_END = 2;

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
};

struct FsCreateMsgData {
	uint64_t mountId {};
	uint8_t nodeType {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsCreateReplyMsgData {
	bool success {};
};

struct FsUnlinkMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsUnlinkReplyMsgData {
	bool success {};
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

struct VfsWriteMsgData {
	uint64_t offset {};
	uint32_t length {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsWriteReplyMsgData {
	bool success {};
	uint32_t bytesWritten {};
	uint64_t size {};
};

struct VfsCreateMsgData {
	uint8_t nodeType {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsCreateReplyMsgData {
	bool success {};
};

struct VfsUnlinkMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsUnlinkReplyMsgData {
	bool success {};
};

struct VfsRenameMsgData {
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct VfsRenameReplyMsgData {
	bool success {};
};

struct VfsTruncateMsgData {
	uint64_t size {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsTruncateReplyMsgData {
	bool success {};
	uint64_t size {};
};

struct VfsOpenMsgData {
	uint32_t flags {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsOpenReplyMsgData {
	bool success {};
	uint64_t handle {};
	uint8_t nodeType {};
	uint64_t size {};
};

struct VfsCloseMsgData {
	uint64_t handle {};
};

struct VfsCloseReplyMsgData {
	bool success {};
};

struct VfsHandleReadMsgData {
	uint64_t handle {};
	uint32_t length {};
};

struct VfsHandleReadReplyMsgData {
	bool success {};
	uint32_t bytesRead {};
	uint64_t position {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

struct VfsHandleWriteMsgData {
	uint64_t handle {};
	uint32_t length {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

struct VfsHandleWriteReplyMsgData {
	bool success {};
	uint32_t bytesWritten {};
	uint64_t position {};
	uint64_t size {};
};

struct VfsHandleSeekMsgData {
	uint64_t handle {};
	int64_t offset {};
	uint8_t whence {};
};

struct VfsHandleSeekReplyMsgData {
	bool success {};
	uint64_t position {};
};

#endif
