#ifndef HORIZONOS_DEVFS_HPP
#define HORIZONOS_DEVFS_HPP

#include "Vfs.hpp"
#include "VfsProtocol.hpp"

#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>

struct VfsDeviceNode {
	std::string name;
	uint64_t ownerPort {};
	uint64_t devicePort {};
	uint32_t permissions {};
};

class DevFs {
public:
	explicit DevFs(std::vector<VfsVolume> &volumes);

	void setPort(uint64_t port);
	void cleanupOwner(uint64_t ownerPort);

	auto registerNode(const std::string &name, uint64_t ownerPort, uint64_t devicePort, uint32_t permissions, VfsDevRegisterReplyMsgData &reply) -> bool;
	auto unregisterNode(const std::string &name, uint64_t ownerPort, VfsDevUnregisterReplyMsgData &reply) -> bool;
	auto stat(const std::string &fsPath, VfsStatReplyMsgData &reply) -> bool;
	auto readDir(const std::string &fsPath, VfsReadDirReplyMsgData &reply) -> bool;
	auto read(const std::string &fsPath, uint64_t offset, uint32_t length, VfsReadReplyMsgData &reply) -> bool;
	auto write(const std::string &fsPath, uint64_t offset, uint32_t length, const uint8_t *data, VfsWriteReplyMsgData &reply) -> bool;
	auto ioctl(const std::string &fsPath, const VfsIoctlMsgData &data, VfsIoctlReplyMsgData &reply) -> bool;

private:
	static void fillName(char *dst, size_t dstSize, size_t &length, const std::string &name);
	static auto nodeName(const std::string &fsPath) -> std::string;

	auto findNode(const std::string &name, VfsDeviceNode &out) -> bool;
	auto volumesText() const -> std::string;

	template<typename Request, typename Reply>
	auto sendDeviceRequest(uint64_t requestType, uint64_t replyType, const VfsDeviceNode &node, Request &request, Reply &reply) -> bool;

	uint64_t vfsPort {};
	std::vector<VfsVolume> &volumes;
	std::vector<VfsDeviceNode> nodes;
	pthread_mutex_t nodesLock = PTHREAD_MUTEX_INITIALIZER;
};

#endif
