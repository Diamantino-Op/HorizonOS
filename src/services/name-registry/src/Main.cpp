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

static uint64_t nrPort = 1;

static std::mutex services_mutex;

void *messageHandlerMain(void *srvsPtr);
void registerService(vector<Service *> *services, uint64_t port, uint64_t ownerPid, uint64_t tid, const string &name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch);
void unregisterService(vector<Service *> *services, string name);

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	const int registerResult = register_horizonos_port(reinterpret_cast<long *>(&nrPort));

	if (registerResult == 0) {
		printf("Name/Registry Service: Successfully registered port!\n");
	} else {
		printf("Name/Registry Service: Failed to register port: %d\n", registerResult);

		return 1;
	}

	auto *services = new vector<Service *>();
	
	pthread_t thread;

	if (pthread_create(&thread, nullptr, messageHandlerMain, services) != 0) {
		delete services;

		return 1;
	}

	pthread_detach(thread);

	while (true) {
		// take a snapshot of the services while protected by the mutex
		vector<Service *> snapshot;
		{
			std::scoped_lock lock(services_mutex);
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

void *registerMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry register message handler!\n");

	auto *response = new RegisterMsgData();
	auto *msg = new hos_msg();
	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ REGISTER_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		msg->buffer = response;
		msg->length = sizeof(RegisterMsgData);

		const int err = receive_horizonos_message(nrPort, msg, nullptr);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive register message: %d\n", err);

			continue;
		}

		if (msg->ret_length < 0 or static_cast<size_t>(msg->ret_length) > sizeof(RegisterMsgData)) {
			printf("Name/Registry Service: Dropped oversized register message (%ld bytes)", msg->ret_length);

			continue;
		}

		if (msg->type != REGISTER_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-register message in register handler (type %lu)", msg->type);

			continue;
		}

		const string name(response->name, response->nameLength);

		bool hasService = false;

		{
			std::scoped_lock lock(services_mutex);
			hasService = ranges::any_of(*services,
				[&](const Service* s) {
					return s && s->name == name;
				});
		}

		if (!hasService) {
			registerService(services, msg->src_port, response->ownerPid, response->tid, name, response->versionMajor, response->versionMinor, response->versionPatch);
		} else {
			printf("Service already registered!");
		}

		auto *newMsg = new hos_msg();

		auto *retData = new RegisterReplyMsgData();

		retData->success = !hasService;

		newMsg->type = REPLY_REGISTER_MSG_TYPE;
		newMsg->port = msg->src_port;
		newMsg->buffer = retData;
		newMsg->length = sizeof(RegisterReplyMsgData);

		send_horizonos_message(nrPort, msg->src_port, newMsg);

		delete newMsg;
	}
}

[[noreturn]] void *unregisterMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry unregister message handler!\n");

	auto *response = new UnregisterMsgData();
	auto *msg = new hos_msg();
	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ UNREGISTER_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		msg->buffer = response;
		msg->length = sizeof(UnregisterMsgData);

		const int err = receive_horizonos_message(nrPort, msg, nullptr);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive unregister message: %d\n", err);

			continue;
		}

		if (msg->ret_length < 0 or static_cast<size_t>(msg->ret_length) > sizeof(UnregisterMsgData)) {
			printf("Name/Registry Service: Dropped oversized unregister message (%ld bytes)", msg->ret_length);

			continue;
		}

		if (msg->type != UNREGISTER_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-unregister message in unregister handler (type %lu)", msg->type);

			continue;
		}

		{
			const string name(response->name, response->nameLength);

			std::scoped_lock lock(services_mutex);
			unregisterService(services, name);
		}
	}
}

[[noreturn]] void *getMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry get message handler!\n");

	auto *response = new GetMsgData();
	auto *msg = new hos_msg();
	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ GET_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		msg->buffer = response;
		msg->length = sizeof(GetMsgData);

		const int err = receive_horizonos_message(nrPort, msg, nullptr);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive get message: %d\n", err);

			continue;
		}

		if (msg->ret_length < 0 or static_cast<size_t>(msg->ret_length) > sizeof(GetMsgData)) {
			printf("Name/Registry Service: Dropped oversized get message (%ld bytes)", msg->ret_length);

			continue;
		}

		if (msg->type != GET_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-get message in get handler (type %lu)", msg->type);

			continue;
		}

		const string name(response->name, response->nameLength);

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

		auto *newMsg = new hos_msg();

		auto *retData = new GetReplyMsgData();

		retData->port = port;
		retData->tid = tid;
		retData->versionMajor = versionMajor;
		retData->versionMinor = versionMinor;
		retData->versionPatch = versionPatch;

		newMsg->type = REPLY_GET_MSG_TYPE;
		newMsg->port = msg->src_port;
		newMsg->buffer = retData;
		newMsg->length = sizeof(GetReplyMsgData);

		send_horizonos_message(nrPort, msg->src_port, newMsg);

		delete newMsg;
	}
}

[[noreturn]] void *checkMsgHandler(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry check message handler!\n");

	auto *response = new CheckMsgData();
	auto *msg = new hos_msg();
	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ CHECK_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		msg->buffer = response;
		msg->length = sizeof(CheckMsgData);

		const int err = receive_horizonos_message(nrPort, msg, nullptr);

		if (err != 0) {
			printf("Name/Registry Service: Failed to receive check message: %d\n", err);

			continue;
		}

		if (msg->ret_length < 0 or static_cast<size_t>(msg->ret_length) > sizeof(CheckMsgData)) {
			printf("Name/Registry Service: Dropped oversized check message (%ld bytes)", msg->ret_length);

			continue;
		}

		if (msg->type != CHECK_MSG_TYPE) {
			printf("Name/Registry Service: Dropped non-check message in check handler (type %lu)", msg->type);

			continue;
		}

		const string name(response->name, response->nameLength);

		bool exists = false;

		{
			std::scoped_lock lock(services_mutex);
			exists = ranges::any_of(*services,
				[&](const Service* s) {
					return s && s->name == name and s->tid == response->tid;
				});
		}

		auto *newMsg = new hos_msg();

		auto *retData = new CheckReplyMsgData();

		retData->exists = exists;

		newMsg->type = REPLY_CHECK_MSG_TYPE;
		newMsg->port = msg->src_port;
		newMsg->buffer = retData;
		newMsg->length = sizeof(CheckReplyMsgData);

		send_horizonos_message(nrPort, msg->src_port, newMsg);

		delete newMsg;
	}
}

void *messageHandlerMain(void *srvsPtr) {
	auto *services = static_cast<vector<Service *> *>(srvsPtr);

	printf("Starting Name/Registry Messaging Service!\n");

	const int registerResult = register_horizonos_port(reinterpret_cast<long *>(&nrPort));

	if (registerResult == 0) {
		printf("Name/Registry Service: Successfully registered port!\n");
	} else {
		printf("Name/Registry Service: Failed to register port: %d\n", registerResult);

		return nullptr;
	}

	while (true) {
		array<char, 1024> receiveBuffer{};
		auto *msg = new hos_msg();

		msg->buffer = receiveBuffer.data();
		msg->length = receiveBuffer.size();

		const int err = receive_horizonos_message(nrPort, msg, nullptr);

		if (err != 0) {
			continue;
		}

		if (msg->ret_length < 0 || static_cast<size_t>(msg->ret_length) > receiveBuffer.size()) {
			printf("Name/Registry Service: Dropped oversized message (%ld bytes)", msg->ret_length);

			continue;
		}

		const string message(receiveBuffer.data(), static_cast<size_t>(msg->ret_length));

		vector<string> parts;
		size_t start = 0;

		while (start <= message.size()) {
			const size_t separator = message.find(';', start);

			if (separator == string::npos) {
				parts.emplace_back(message.substr(start));
				break;
			}

			parts.emplace_back(message.substr(start, separator - start));
			start = separator + 1;
		}

		if (parts.empty()) {
			continue;
		}

		if (msg->type == REGISTER_MSG_TYPE) {
			if (parts.size() < 7) {
				continue;
			}

			bool hasService = false;
			{
				std::scoped_lock lock(services_mutex);
				hasService = ranges::any_of(*services,
					[&](const Service* s) {
						return s && s->name == parts[3];
					});
			}

			if (!hasService) {
				registerService(services, msg->src_port, stoul(parts[1]), stoul(parts[2]), parts[3], stoul(parts[4]), stoul(parts[5]), stoul(parts[6]));
			} else {
				printf("Service already registered!");
			}

			auto *newMsg = new hos_msg();

			string ret = to_string(hasService ? 0 : 1);

			newMsg->type = REPLY_MSG_TYPE;
			newMsg->port = msg->src_port;
			newMsg->buffer = static_cast<void *>(ret.data());
			newMsg->length = ret.size();

			send_horizonos_message(nrPort, msg->src_port, newMsg);

			delete newMsg;
		}

		if (msg->type == UNREGISTER_MSG_TYPE) {
			if (parts.size() < 1) {
				continue;
			}

			// TODO: Implement security
			{
				std::scoped_lock lock(services_mutex);
				unregisterService(services, parts[1]);
			}
		}

		delete msg;
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