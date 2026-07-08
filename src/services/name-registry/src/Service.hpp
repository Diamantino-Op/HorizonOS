#ifndef HORIZONOS_SERVICE_HPP
#define HORIZONOS_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t UNREGISTER_MSG_TYPE = 0x2;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

struct RegisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct UnregisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
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

class NameRegistryService {
public:
	auto start() -> int;
};

struct Service {
	uint64_t port;
	uint64_t ownerPid;
	uint64_t tid;
	string name;
	uint64_t versionMajor;
	uint64_t versionMinor;
	uint64_t versionPatch;

	Service(const uint64_t port, const uint64_t ownerPid, const uint64_t tid, const string &name, const uint64_t versionMajor, const uint64_t versionMinor, const uint64_t versionPatch) :
		port(port),
		ownerPid(ownerPid),
		tid(tid),
		name(name),
		versionMajor(versionMajor),
		versionMinor(versionMinor),
		versionPatch(versionPatch) {}
};

class NameRegistryUtils {
public:
	static auto validServiceName(const string &name) -> bool {
		if (name.empty() or name == "none" or name == "NAME") {
			return false;
		}

		return true;
	}

	template <typename MsgT>
	static auto extractServiceName(const MsgT *msg, string &name) -> bool {
		if (msg == nullptr or msg->nameLength == 0 or msg->nameLength > sizeof(msg->name)) {
			return false;
		}

		if (msg->name[msg->nameLength - 1] != '\0') {
			return false;
		}

		name.assign(msg->name, msg->nameLength - 1);

		return validServiceName(name);
	}

	static void registerService(vector<Service> *services, uint64_t port, uint64_t ownerPid, uint64_t tid, const string &name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch);
	static void unregisterService(vector<Service> *services, string name);
	static auto unregisterService(vector<Service> *services, const Service &expected) -> bool;
};

#endif
