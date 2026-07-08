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
constexpr uint64_t FS_MKDIR_MSG_TYPE       = 0x80012;
constexpr uint64_t FS_MKDIR_REPLY_MSG_TYPE = 0x80013;
constexpr uint64_t FS_SYNC_MSG_TYPE        = 0x80014;
constexpr uint64_t FS_SYNC_REPLY_MSG_TYPE  = 0x80015;
constexpr uint64_t FS_LINK_MSG_TYPE        = 0x80016;
constexpr uint64_t FS_LINK_REPLY_MSG_TYPE  = 0x80017;
constexpr uint64_t FS_SYMLINK_MSG_TYPE     = 0x80018;
constexpr uint64_t FS_SYMLINK_REPLY_MSG_TYPE = 0x80019;
constexpr uint64_t FS_READLINK_MSG_TYPE    = 0x8001A;
constexpr uint64_t FS_READLINK_REPLY_MSG_TYPE = 0x8001B;

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
constexpr uint64_t VFS_MKDIR_MSG_TYPE      = 0x9001A;
constexpr uint64_t VFS_MKDIR_REPLY_MSG_TYPE = 0x9001B;
constexpr uint64_t VFS_MOUNT_REFRESH_MSG_TYPE = 0x9001C;
constexpr uint64_t VFS_MOUNT_REFRESH_REPLY_MSG_TYPE = 0x9001D;
constexpr uint64_t VFS_UNMOUNT_MSG_TYPE    = 0x9001E;
constexpr uint64_t VFS_UNMOUNT_REPLY_MSG_TYPE = 0x9001F;
constexpr uint64_t VFS_LOCK_MSG_TYPE       = 0x90020;
constexpr uint64_t VFS_LOCK_REPLY_MSG_TYPE = 0x90021;
constexpr uint64_t VFS_UNLOCK_MSG_TYPE     = 0x90022;
constexpr uint64_t VFS_UNLOCK_REPLY_MSG_TYPE = 0x90023;
constexpr uint64_t VFS_SYNC_MSG_TYPE       = 0x90024;
constexpr uint64_t VFS_SYNC_REPLY_MSG_TYPE = 0x90025;
constexpr uint64_t VFS_FSYNC_MSG_TYPE      = 0x90026;
constexpr uint64_t VFS_FSYNC_REPLY_MSG_TYPE = 0x90027;
constexpr uint64_t VFS_COPY_MSG_TYPE       = 0x90028;
constexpr uint64_t VFS_COPY_REPLY_MSG_TYPE = 0x90029;
constexpr uint64_t VFS_LINK_MSG_TYPE       = 0x9002A;
constexpr uint64_t VFS_LINK_REPLY_MSG_TYPE = 0x9002B;
constexpr uint64_t VFS_SYMLINK_MSG_TYPE    = 0x9002C;
constexpr uint64_t VFS_SYMLINK_REPLY_MSG_TYPE = 0x9002D;
constexpr uint64_t VFS_DEV_REGISTER_MSG_TYPE = 0x9002E;
constexpr uint64_t VFS_DEV_REGISTER_REPLY_MSG_TYPE = 0x9002F;
constexpr uint64_t VFS_DEV_UNREGISTER_MSG_TYPE = 0x90030;
constexpr uint64_t VFS_DEV_UNREGISTER_REPLY_MSG_TYPE = 0x90031;
constexpr uint64_t VFS_IOCTL_MSG_TYPE      = 0x90032;
constexpr uint64_t VFS_IOCTL_REPLY_MSG_TYPE = 0x90033;
constexpr uint64_t VFS_HANDLE_READDIR_MSG_TYPE = 0x90034;
constexpr uint64_t VFS_HANDLE_READDIR_REPLY_MSG_TYPE = 0x90035;
constexpr uint64_t VFS_READLINK_MSG_TYPE   = 0x90036;
constexpr uint64_t VFS_READLINK_REPLY_MSG_TYPE = 0x90037;
constexpr uint64_t VFS_HANDLE_TRUNCATE_MSG_TYPE = 0x90038;
constexpr uint64_t VFS_HANDLE_TRUNCATE_REPLY_MSG_TYPE = 0x90039;
constexpr uint64_t VFS_DEV_READ_MSG_TYPE   = 0x9003A;
constexpr uint64_t VFS_DEV_READ_REPLY_MSG_TYPE = 0x9003B;
constexpr uint64_t VFS_DEV_WRITE_MSG_TYPE  = 0x9003C;
constexpr uint64_t VFS_DEV_WRITE_REPLY_MSG_TYPE = 0x9003D;
constexpr uint64_t VFS_DEV_IOCTL_MSG_TYPE  = 0x9003E;
constexpr uint64_t VFS_DEV_IOCTL_REPLY_MSG_TYPE = 0x9003F;
constexpr uint64_t VFS_REGISTER_FS_HANDLER_MSG_TYPE = 0x90040;
constexpr uint64_t VFS_REGISTER_FS_HANDLER_REPLY_MSG_TYPE = 0x90041;

constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint32_t STORAGE_MAX_LIST_DEVICES = 64;
constexpr uint32_t VFS_MAX_PATH_LENGTH = 256;
constexpr uint32_t VFS_MAX_NAME_LENGTH = 64;
constexpr uint32_t VFS_MAX_READ_SIZE = 2048;
constexpr uint32_t VFS_MAX_DIR_ENTRIES = 32;

constexpr uint8_t VFS_NODE_UNKNOWN = 0;
constexpr uint8_t VFS_NODE_FILE = 1;
constexpr uint8_t VFS_NODE_DIRECTORY = 2;
constexpr uint8_t VFS_NODE_SYMLINK = 3;
constexpr uint8_t VFS_NODE_DEVICE = 4;

constexpr uint32_t VFS_OPEN_READ = 1 << 0;
constexpr uint32_t VFS_OPEN_WRITE = 1 << 1;
constexpr uint32_t VFS_OPEN_CREATE = 1 << 2;
constexpr uint32_t VFS_OPEN_APPEND = 1 << 3;
constexpr uint32_t VFS_OPEN_TRUNCATE = 1 << 4;
constexpr uint32_t VFS_OPEN_EXCLUSIVE = 1 << 5;

constexpr uint8_t VFS_SEEK_SET = 0;
constexpr uint8_t VFS_SEEK_CUR = 1;
constexpr uint8_t VFS_SEEK_END = 2;

constexpr uint8_t VFS_LOCK_SHARED = 1;
constexpr uint8_t VFS_LOCK_EXCLUSIVE = 2;

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

struct VfsRegisterFsHandlerMsgData {
	uint64_t handlerPort {};
	char fsName[16] {};
	size_t fsNameLength {};
};

struct VfsRegisterFsHandlerReplyMsgData {
	bool success {};
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
	uint64_t nodeId {};
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

struct FsSyncMsgData {
	uint64_t mountId {};
};

struct FsSyncReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct FsLinkMsgData {
	uint64_t mountId {};
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct FsLinkReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct FsSymlinkMsgData {
	uint64_t mountId {};
	char target[VFS_MAX_PATH_LENGTH] {};
	size_t targetLength {};
	char linkPath[VFS_MAX_PATH_LENGTH] {};
	size_t linkPathLength {};
};

struct FsSymlinkReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct FsReadLinkMsgData {
	uint64_t mountId {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct FsReadLinkReplyMsgData {
	bool success {};
	uint32_t status {};
	char target[VFS_MAX_PATH_LENGTH] {};
	size_t targetLength {};
};

struct VfsStatMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsStatReplyMsgData {
	bool success {};
	uint8_t nodeType {};
	uint64_t size {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsReadDirMsgData {
	uint32_t offset {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsReadDirReplyMsgData {
	bool success {};
	uint32_t entryCount {};
	VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {};
	uint32_t status {};
	uint32_t nextOffset {};
	bool hasMore {};
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
	uint32_t status {};
};

struct VfsCreateMsgData {
	uint8_t nodeType {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsCreateReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsMkdirMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsMkdirReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsUnlinkMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsUnlinkReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsRenameMsgData {
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct VfsRenameReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsTruncateMsgData {
	uint64_t size {};
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsTruncateReplyMsgData {
	bool success {};
	uint64_t size {};
	uint32_t status {};
};

struct VfsReadLinkMsgData {
	char path[VFS_MAX_PATH_LENGTH] {};
	size_t pathLength {};
};

struct VfsReadLinkReplyMsgData {
	bool success {};
	uint32_t status {};
	char target[VFS_MAX_PATH_LENGTH] {};
	size_t targetLength {};
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
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsCloseMsgData {
	uint64_t handle {};
};

struct VfsCloseReplyMsgData {
	bool success {};
	uint32_t status {};
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
	uint32_t status {};
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
	uint32_t status {};
};

struct VfsHandleSeekMsgData {
	uint64_t handle {};
	int64_t offset {};
	uint8_t whence {};
};

struct VfsHandleSeekReplyMsgData {
	bool success {};
	uint64_t position {};
	uint32_t status {};
};

struct VfsHandleReadDirMsgData {
	uint64_t handle {};
};

struct VfsHandleReadDirReplyMsgData {
	bool success {};
	uint32_t entryCount {};
	VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {};
	uint64_t position {};
	bool hasMore {};
	uint32_t status {};
};

struct VfsHandleTruncateMsgData {
	uint64_t handle {};
	uint64_t size {};
};

struct VfsHandleTruncateReplyMsgData {
	bool success {};
	uint64_t size {};
	uint32_t status {};
};

struct VfsDeviceReadMsgData {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint64_t offset {};
	uint32_t length {};
};

struct VfsDeviceReadReplyMsgData {
	bool success {};
	uint32_t status {};
	uint32_t bytesRead {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

struct VfsDeviceWriteMsgData {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint64_t offset {};
	uint32_t length {};
	uint8_t data[VFS_MAX_READ_SIZE] {};
};

struct VfsDeviceWriteReplyMsgData {
	bool success {};
	uint32_t status {};
	uint32_t bytesWritten {};
};

struct VfsDeviceIoctlMsgData {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint32_t request {};
	uint32_t inputLength {};
	uint8_t input[VFS_MAX_READ_SIZE] {};
};

struct VfsDeviceIoctlReplyMsgData {
	bool success {};
	uint32_t status {};
	uint32_t outputLength {};
	uint8_t output[VFS_MAX_READ_SIZE] {};
};

struct VfsMountRefreshMsgData {
	uint32_t reserved {};
};

struct VfsMountRefreshReplyMsgData {
	bool success {};
	uint32_t status {};
	uint32_t volumeCount {};
};

struct VfsUnmountMsgData {
	char volume[VFS_MAX_NAME_LENGTH] {};
	size_t volumeLength {};
};

struct VfsUnmountReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsLockMsgData {
	uint64_t handle {};
	uint64_t offset {};
	uint64_t length {};
	uint8_t mode {};
};

struct VfsLockReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsUnlockMsgData {
	uint64_t handle {};
	uint64_t offset {};
	uint64_t length {};
};

struct VfsUnlockReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsSyncMsgData {
	char volume[VFS_MAX_NAME_LENGTH] {};
	size_t volumeLength {};
};

struct VfsSyncReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsFsyncMsgData {
	uint64_t handle {};
};

struct VfsFsyncReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsCopyMsgData {
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct VfsCopyReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t bytesCopied {};
};

struct VfsLinkMsgData {
	char oldPath[VFS_MAX_PATH_LENGTH] {};
	size_t oldPathLength {};
	char newPath[VFS_MAX_PATH_LENGTH] {};
	size_t newPathLength {};
};

struct VfsLinkReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsSymlinkMsgData {
	char target[VFS_MAX_PATH_LENGTH] {};
	size_t targetLength {};
	char linkPath[VFS_MAX_PATH_LENGTH] {};
	size_t linkPathLength {};
};

struct VfsSymlinkReplyMsgData {
	bool success {};
	uint32_t status {};
	uint64_t nodeId {};
};

struct VfsDevRegisterMsgData {
	uint64_t devicePort {};
	uint32_t permissions {};
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
};

struct VfsDevRegisterReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsDevUnregisterMsgData {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
};

struct VfsDevUnregisterReplyMsgData {
	bool success {};
	uint32_t status {};
};

struct VfsIoctlMsgData {
	uint64_t handle {};
	uint32_t request {};
	uint32_t inputLength {};
	uint8_t input[VFS_MAX_READ_SIZE] {};
};

struct VfsIoctlReplyMsgData {
	bool success {};
	uint32_t status {};
	uint32_t outputLength {};
	uint8_t output[VFS_MAX_READ_SIZE] {};
};

#endif
