#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <ranges>
#include <algorithm>
#include <unistd.h>
#include <mutex>

#include "horizonos/generic.h"
#include "abi-bits/hos_msg.h"

#include "Service.hpp"

using namespace std;
using namespace std::ranges;

// TODO: Move to header
constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t UNREGISTER_MSG_TYPE = 0x2;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

// Name max 16 chars
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
	uint16_t tid {};
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

template <typename MsgT>
bool extractServiceName(const MsgT *msg, string &name) {
	if (msg == nullptr || msg->nameLength == 0 || msg->nameLength > sizeof(msg->name)) {
		return false;
	}

	if (msg->name[msg->nameLength - 1] != '\0') {
		return false;
	}

	name.assign(msg->name, msg->nameLength - 1);
	return true;
}

uint64_t nrPort = 0;

static std::mutex services_mutex;

[[noreturn]] void *registerMsgHandler(void *srvsPtr);
[[noreturn]] void *unregisterMsgHandler(void *srvsPtr);
[[noreturn]] void *getMsgHandler(void *srvsPtr);
[[noreturn]] void *checkMsgHandler(void *srvsPtr);

void registerService(vector<Service *> *services, uint64_t port, uint64_t ownerPid, uint64_t tid, const string &name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch);
void unregisterService(vector<Service *> *services, string name);

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	const int registerResult = register_horizonos_port(reinterpret_cast<long *>(&nrPort), 1);

	if (registerResult == 0) {
		printf("Name/Registry Service: Successfully registered port: %lu!\n", nrPort);
	} else {
		printf("Name/Registry Service: Failed to register port: %d\n", registerResult);

		return 1;
	}

	auto *services = new vector<Service *>();
	
	pthread_t registerThread;
	pthread_t unregisterThread;
	pthread_t getThread;
	pthread_t checkThread;

	const int registerThreadResult = pthread_create(&registerThread, nullptr, registerMsgHandler, services);
	const int unregisterThreadResult = pthread_create(&unregisterThread, nullptr, unregisterMsgHandler, services);
	const int getThreadResult = pthread_create(&getThread, nullptr, getMsgHandler, services);
	const int checkThreadResult = pthread_create(&checkThread, nullptr, checkMsgHandler, services);

	if (registerThreadResult != 0 or unregisterThreadResult != 0 or getThreadResult != 0 or checkThreadResult != 0) {
		delete services;

		return 1;
	}

	pthread_detach(registerThread);
	pthread_detach(unregisterThread);
	pthread_detach(getThread);
	pthread_detach(checkThread);

	for (;;) {
		// take a snapshot of the services while protected by the mutex
		vector<Service *> snapshot;
		{
			const std::scoped_lock lock(services_mutex);
			snapshot = *services;
		}

		for (const auto *service : snapshot) {
			bool ret = false;

			int err = is_thread_alive(service->tid, &ret);

			if (err == 0 and !ret) {
				printf("Service: %s dead, unregistering it!", service->name.c_str());

				// unregister modifies the services vector, so lock while calling it
				std::scoped_lock lock(services_mutex);
				unregisterService(services, service->name);
			}
		}

		usleep(100000);
	}

	delete services;

	return 0;
}

// TODO: Probable culprit of receive heap corruption
[[noreturn]] void *registerMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry register message handler!\n");

	auto response = RegisterMsgData();
	auto msg = hos_msg();

	msg.buffer = &response;
	msg.length = sizeof(RegisterMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ REGISTER_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int err = receive_horizonos_message(nrPort, &msg, &filterOptions);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive register message: %d\n", err);

			continue;
		}

		if (msg.ret_length < 0 or static_cast<size_t>(msg.ret_length) != sizeof(RegisterMsgData)) {
			printf("Name/Registry Service: Dropped wrong register message (%ld bytes)", msg.ret_length);

			continue;
		}

		if (msg.type != REGISTER_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-register message in register handler (type %lu)", msg.type);

			continue;
		}

		string name;

		if (!extractServiceName(&response, name)) {
			printf("Name/Registry Service: Dropped invalid register message name length (%zu)\n", response.nameLength);

			continue;
		}

		bool hasService = false;

		{
			std::scoped_lock lock(services_mutex);

			hasService = ranges::any_of(*services,
				[&](const Service* s) {
					return s && s->name == name;
				});
		}

		if (!hasService) {
			registerService(services, msg.src_port, response.ownerPid, response.tid, name, response.versionMajor, response.versionMinor, response.versionPatch);
		} else {
			printf("Service already registered!");
		}

		auto newMsg = hos_msg();

		auto retData = RegisterReplyMsgData();

		retData.success = !hasService;

		newMsg.type = REPLY_REGISTER_MSG_TYPE;
		newMsg.port = msg.src_port;
		newMsg.buffer = &retData;
		newMsg.length = sizeof(RegisterReplyMsgData);

		send_horizonos_message(nrPort, msg.src_port, &newMsg);

		//delete retData;
		//delete newMsg;
	}
}

[[noreturn]] void *unregisterMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry unregister message handler!\n");

	auto response = UnregisterMsgData();
	auto msg = hos_msg();

	msg.buffer = &response;
	msg.length = sizeof(UnregisterMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ UNREGISTER_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int err = receive_horizonos_message(nrPort, &msg, &filterOptions);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive unregister message: %d\n", err);

			continue;
		}

		if (msg.ret_length < 0 or static_cast<size_t>(msg.ret_length) != sizeof(UnregisterMsgData)) {
			printf("Name/Registry Service: Dropped wrong unregister message (%ld bytes)", msg.ret_length);

			continue;
		}

		if (msg.type != UNREGISTER_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-unregister message in unregister handler (type %lu)", msg.type);

			continue;
		}

		{
			string name;

			if (!extractServiceName(&response, name)) {
				printf("Name/Registry Service: Dropped invalid unregister message name length (%zu)\n", response.nameLength);

				continue;
			}

			std::scoped_lock lock(services_mutex);
			unregisterService(services, name);
		}
	}
}

[[noreturn]] void *getMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry get message handler!\n");

	auto response = GetMsgData();
	auto msg = hos_msg();

	msg.buffer = &response;
	msg.length = sizeof(GetMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ GET_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int err = receive_horizonos_message(nrPort, &msg, &filterOptions);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive get message: %d\n", err);

			continue;
		}

		if (msg.ret_length < 0 or static_cast<size_t>(msg.ret_length) != sizeof(GetMsgData)) {
			printf("Name/Registry Service: Dropped wrong get message (%ld bytes)", msg.ret_length);

			continue;
		}

		if (msg.type != GET_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-get message in get handler (type %lu)", msg.type);

			continue;
		}

		string name;

		if (!extractServiceName(&response, name)) {
			printf("Name/Registry Service: Dropped invalid get message name length (%zu)\n", response.nameLength);

			continue;
		}

		uint64_t port = 0;
		uint16_t tid = 0;
		uint16_t versionMajor = 0;
		uint16_t versionMinor = 0;
		uint16_t versionPatch = 0;

		{
			std::scoped_lock lock(services_mutex);

			const auto res = ranges::find_if(*services,
				[&](const Service* s) {
					return s && s->name == name;
				});

			if (res != services->end()) {
				const Service *srv = *res;

				port = srv->port;
				tid = srv->tid;
				versionMajor = srv->versionMajor;
				versionMinor = srv->versionMinor;
				versionPatch = srv->versionPatch;
			}
		}

		auto newMsg = hos_msg();

		auto retData = GetReplyMsgData();

		retData.port = port;
		retData.tid = tid;
		retData.versionMajor = versionMajor;
		retData.versionMinor = versionMinor;
		retData.versionPatch = versionPatch;

		newMsg.type = REPLY_GET_MSG_TYPE;
		newMsg.port = msg.src_port;
		newMsg.buffer = &retData;
		newMsg.length = sizeof(GetReplyMsgData);

		send_horizonos_message(nrPort, msg.src_port, &newMsg);

		//delete retData;
		//delete newMsg;
	}
}

[[noreturn]] void *checkMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry check message handler!\n");

	auto response = CheckMsgData();
	auto msg = hos_msg();

	msg.buffer = &response;
	msg.length = sizeof(CheckMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ CHECK_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int err = receive_horizonos_message(nrPort, &msg, &filterOptions);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive check message: %d\n", err);

			continue;
		}

		if (msg.ret_length < 0 or static_cast<size_t>(msg.ret_length) != sizeof(CheckMsgData)) {
			printf("Name/Registry Service: Dropped wrong check message (%ld bytes)", msg.ret_length);

			continue;
		}

		if (msg.type != CHECK_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-check message in check handler (type %lu)", msg.type);

			continue;
		}

		string name;

		if (!extractServiceName(&response, name)) {
			printf("Name/Registry Service: Dropped invalid check message name length (%zu)\n", response.nameLength);

			continue;
		}

		bool exists = false;

		{
			std::scoped_lock lock(services_mutex);

			exists = ranges::any_of(*services,
				[&](const Service* s) {
					return s and (s->name == name or s->tid == response.tid);
				});
		}

		auto newMsg = hos_msg();

		auto retData = CheckReplyMsgData();

		retData.exists = exists;

		newMsg.type = REPLY_CHECK_MSG_TYPE;
		newMsg.port = msg.src_port;
		newMsg.buffer = &retData;
		newMsg.length = sizeof(CheckReplyMsgData);

		send_horizonos_message(nrPort, msg.src_port, &newMsg);

		//delete retData;
		//delete newMsg;
	}
}

void registerService(vector<Service *> *services, const uint64_t port, const uint64_t ownerPid, const uint64_t tid, const string &name, const uint64_t versionMajor, const uint64_t versionMinor, const uint64_t versionPatch) {
	std::scoped_lock lock(services_mutex);
	services->push_back(new Service(port, ownerPid, tid, name, versionMajor, versionMinor, versionPatch));

	printf("Service %s registered on port %lu!\n", name.c_str(), port);
}

void unregisterService(vector<Service *> *services, string name) {
	std::scoped_lock lock(services_mutex);
	erase_if(*services, [name](const Service *service) { return service->name == name; });

	printf("Service %s unregistered!\n", name.c_str());
}