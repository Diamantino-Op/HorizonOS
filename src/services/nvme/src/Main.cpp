#include "NVMe.hpp"

#include "horizonos/generic.h"
#include "unistd.h"

#include <vector>
#include <cstdio>
#include <cstring>
#include <string>

using namespace std;

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;
constexpr uint64_t PCI_SEARCH_DEVICE_MSG_TYPE = 0xD0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE = 0xE0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_MSG_TYPE = 0xF0;

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

struct PciSearchDeviceMsgData {
	uint8_t pciClass {};
	uint8_t pciSubclass {};
	uint8_t pciProg {};
};

uint64_t nvmePort = 0;
uint64_t pciPort = 0;
uint64_t pciTid = 0;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	const int registerResult = register_horizonos_port(reinterpret_cast<long *>(&nvmePort));

	if (registerResult == 0) {
		printf("NVMe: Successfully registered port!\n");
	} else {
		printf("NVMe: Failed to register port: %d\n", registerResult);

		return 1;
	}

	{
		// Send

		auto newMsg = hos_msg();

		auto registerData = RegisterMsgData();

		registerData.ownerPid = getpid();
		registerData.tid = static_cast<uint16_t>(gettid());
		strncpy(registerData.name, string("NVMe").c_str(), sizeof(registerData.name) - 1);
		registerData.name[sizeof(registerData.name) - 1] = '\0';
		registerData.nameLength = strlen(registerData.name) + 1;

		newMsg.type = REGISTER_MSG_TYPE;
		newMsg.port = 1;
		newMsg.buffer = &registerData;
		newMsg.length = sizeof(RegisterMsgData);

		send_horizonos_message(nvmePort, 1, &newMsg);

		// Receive

		auto recvMsg = hos_msg();

		auto registerResData = RegisterReplyMsgData();

		recvMsg.buffer = &registerResData;
		recvMsg.length = sizeof(RegisterReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_REGISTER_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(nvmePort, &recvMsg, &filterOptions);

		if (srvRegisterResult == 0 and registerResData.success) {
			printf("NVMe: Successfully registered service!\n");
		} else {
			printf("NVMe: Failed to register service: %d\n", srvRegisterResult);

			delete[] filterOptions.whiteListTypes;

			return 1;
		}

		delete[] filterOptions.whiteListTypes;
	}

	{
		// Send

		auto checkMsg = hos_msg();

		auto checkData = CheckMsgData();

		strncpy(checkData.name, string("PCI").c_str(), sizeof(checkData.name) - 1);
		checkData.name[sizeof(checkData.name) - 1] = '\0';
		checkData.nameLength = strlen(checkData.name) + 1;

		checkMsg.type = CHECK_MSG_TYPE;
		checkMsg.port = 1;
		checkMsg.buffer = &checkData;
		checkMsg.length = sizeof(CheckMsgData);

		// Reply

		auto recvCheckMsg = hos_msg();

		auto checkResData = CheckReplyMsgData();

		recvCheckMsg.buffer = &checkResData;
		recvCheckMsg.length = sizeof(CheckReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_CHECK_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		for (;;) {
			send_horizonos_message(nvmePort, 1, &checkMsg);

			const int srvRegisterResult = receive_horizonos_message(nvmePort, &recvCheckMsg, &filterOptions);

			if (srvRegisterResult == 0 and checkResData.exists) {
				break;
			}

			usleep(10000);
		}

		delete[] filterOptions.whiteListTypes;
	}

	{
		// Send

		auto getMsg = hos_msg();

		auto getData = GetMsgData();

		strncpy(getData.name, string("PCI").c_str(), sizeof(getData.name) - 1);
		getData.name[sizeof(getData.name) - 1] = '\0';
		getData.nameLength = strlen(getData.name) + 1;

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &getData;
		getMsg.length = sizeof(GetMsgData);

		send_horizonos_message(nvmePort, 1, &getMsg);

		// Reply

		auto recvGetMsg = hos_msg();

		auto getResData = GetReplyMsgData();

		recvGetMsg.buffer = &getResData;
		recvGetMsg.length = sizeof(GetReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_GET_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(nvmePort, &recvGetMsg, &filterOptions);

		if (srvRegisterResult != 0) {
			printf("NVMe: Failed to get PCI port!\n");

			delete[] filterOptions.whiteListTypes;

			return 1;
		}

		printf("NVMe: PCI info: Port: %lu, TID: %u, Version: %u.%u.%u.\n", getResData.port, getResData.tid, getResData.versionMajor, getResData.versionMinor, getResData.versionPatch);

		pciPort = getResData.port;
		pciTid = getResData.tid;

		delete[] filterOptions.whiteListTypes;
	}

	vector<PciDevice> nvmeDevices {};

	{
		printf("NVMe: Requesting PCI service to search for NVMe devices...\n");

		// Send

		auto searchMsg = hos_msg();

		auto searchData = PciSearchDeviceMsgData();

		searchData.pciClass = 0x01; // Mass Storage
		searchData.pciSubclass = 0x08; // Non-Volatile Memory
		searchData.pciProg = 0x02; // NVM Express

		searchMsg.type = PCI_SEARCH_DEVICE_MSG_TYPE;
		searchMsg.port = pciPort;
		searchMsg.buffer = &searchData;
		searchMsg.length = sizeof(PciSearchDeviceMsgData);

		send_horizonos_message(nvmePort, pciPort, &searchMsg);

		// Start Reply

		auto recvStartSearchMsg = hos_msg();

		uint64_t getStartSearchData = 0;

		recvStartSearchMsg.buffer = &getStartSearchData;
		recvStartSearchMsg.length = sizeof(uint64_t);

		auto startFilterOptions = filter_options();

		startFilterOptions.whiteListTypes = new uint64_t[1]{ PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE };
		startFilterOptions.whiteListCount = 1;

		// Reply

		auto recvSearchMsg = hos_msg();

		auto getSearchData = PciDevice();

		recvSearchMsg.buffer = &getSearchData;
		recvSearchMsg.length = sizeof(PciDevice);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ PCI_SEARCH_DEVICE_REPLY_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int startMsgResult = receive_horizonos_message(nvmePort, &recvStartSearchMsg, &startFilterOptions);

		if (startMsgResult != 0) {
			printf("NVMe: Failed to receive PCI search start message!\n");

			return 1;
		}

		printf("NVMe: PCI service will send %lu NVMe device(s).\n", getStartSearchData);

		for (uint64_t i = 0; i < getStartSearchData; ++i) {
			const int msgResult = receive_horizonos_message(nvmePort, &recvSearchMsg, &filterOptions);

			if (msgResult != 0) {
				printf("NVMe: Failed to receive PCI search device message!\n");

				return 1;
			}

			nvmeDevices.push_back(getSearchData);
		}

		printf("NVMe: Received %zu NVMe device(s) from PCI service.\n", nvmeDevices.size());

		delete[] startFilterOptions.whiteListTypes;
		delete[] filterOptions.whiteListTypes;
	}

	for (;;) {}

	return 0;
}