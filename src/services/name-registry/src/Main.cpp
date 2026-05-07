#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <ranges>
#include <algorithm>
#include <unistd.h>

#include "horizonos/generic.h"
#include "abi-bits/hos_msg.h"

#include "Service.hpp"

using namespace std;
using namespace std::ranges;

void messageHandlerMain();
void registerService(uint64_t port, uint64_t ownerPid, uint64_t tid, const string &name, uint64_t versionMajor, uint64_t versionMinor, uint64_t versionPatch);
void unregisterService(string name);

vector<Service *> *services;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	services = new vector<Service *>();
	const thread messageHandler(messageHandlerMain);

	//int handlerTid = messageHandler.get_id();

	while (true) {
		for (const auto *service : *services) {
			bool ret = false;

			int err = is_thread_alive(service->tid, &ret);

			if (err == 0 and !ret) {
				printf("Service: %s dead, unregistering it!\r\n", service->name.c_str());

				unregisterService(service->name);
			}
		}

		usleep(100000);
	}

	return 0;
}

void messageHandlerMain() {
	printf("Starting Name/Registry Messaging Service!\r\n");

	const int registerResult = register_horizonos_port(1);

	if (registerResult == 0) {
		printf("Name/Registry Service: Successfully registered port!\r\n");
	} else {
		printf("Name/Registry Service: Failed to register port: %d\r\n", registerResult);

		return;
	}

	while (true) {
		array<char, 1024> receiveBuffer{};
		auto *msg = new hos_msg();

		msg->buffer = receiveBuffer.data();
		msg->length = receiveBuffer.size();

		const int err = receive_horizonos_message(1, msg);

		if (err != 0) {
			continue;
		}

		if (msg->ret_length < 0 || static_cast<size_t>(msg->ret_length) > receiveBuffer.size()) {
			printf("Name/Registry Service: Dropped oversized message (%ld bytes)\r\n", msg->ret_length);

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

		if (parts[0] == "register") {
			if (parts.size() < 7) {
				continue;
			}

			const bool hasService = ranges::any_of(*services,
				[&](const Service* s) {
					return s && s->name == parts[3];
				});

			printf("A!\r\n");

			if (!hasService) {
				printf("B!\r\n");
				registerService(msg->src_port, stoul(parts[1]), stoul(parts[2]), parts[3], stoul(parts[4]), stoul(parts[5]), stoul(parts[6]));
			} else {
				printf("Service already registered!\r\n");
			}

			auto *newMsg = new hos_msg();

			string ret = to_string(hasService ? 0 : 1);

			newMsg->port = msg->src_port;
			newMsg->buffer = static_cast<void *>(ret.data());
			newMsg->length = ret.size();

			send_horizonos_message(msg->src_port, newMsg);

			delete newMsg;
		}

		if (parts[0] == "unregister") {
			if (parts.size() < 2) {
				continue;
			}

			// TODO: Implement security
			unregisterService(parts[1]);
		}

		if (parts[0] == "get") {
			if (parts.size() < 2) {
				continue;
			}

			const auto res = ranges::find_if(*services,
				[&](const Service* s) {
					return s && s->name == parts[1];
				});

			uint64_t port = 0;

			if (res != services->end()) {
				const Service *srv = *res;

				port = srv->port;
			}

			auto *newMsg = new hos_msg();

			string ret = to_string(port);

			newMsg->port = msg->src_port;
			newMsg->buffer = static_cast<void *>(ret.data());
			newMsg->length = ret.size();

			send_horizonos_message(msg->src_port, newMsg);

			delete newMsg;
		}

		if (parts[0] == "check") {
			if (parts.size() < 3) {
				continue;
			}

			const bool exists = ranges::any_of(*services,
				[&](const Service* s) {
					return s && s->name == parts[1] && s->tid == stoull(parts[2]);
				});

			auto *newMsg = new hos_msg();

			string ret = to_string(exists ? 1 : 0);

			newMsg->port = msg->src_port;
			newMsg->buffer = static_cast<void *>(ret.data());
			newMsg->length = ret.size();

			send_horizonos_message(msg->src_port, newMsg);

			delete newMsg;
		}

		delete msg;

		break;
	}
}

void registerService(const uint64_t port, const uint64_t ownerPid, const uint64_t tid, const string &name, const uint64_t versionMajor, const uint64_t versionMinor, const uint64_t versionPatch) {
	services->push_back(new Service(port, ownerPid, tid, name, versionMajor, versionMinor, versionPatch));

	printf("Service %s registered on port %lu!\r\n", name.c_str(), port);
}

void unregisterService(string name) {
	erase_if(*services, [name](const Service *service) { return service->name == name; });

	printf("Service %s unregistered!\r\n", name.c_str());
}