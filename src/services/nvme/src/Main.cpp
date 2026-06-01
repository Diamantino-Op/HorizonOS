#include "NVMe.hpp"

#include "horizonos/generic.h"
#include "unistd.h"
#include "pthread.h"

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
		printf("NVMe: Successfully registered port!");
		fflush(stdout);
	} else {
		printf("NVMe: Failed to register port: %d", registerResult);
		fflush(stdout);

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

		const int sendNewRet = send_horizonos_message(nvmePort, 1, &newMsg);

		if (sendNewRet != 0) {
			printf("NVMe: Failed to send register message: %d", sendNewRet);
			fflush(stdout);

			return 1;
		}

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
			printf("NVMe: Successfully registered service!");
			fflush(stdout);
		} else {
			printf("NVMe: Failed to register service: %d", srvRegisterResult);
			fflush(stdout);

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
			const int sendNewRet = send_horizonos_message(nvmePort, 1, &checkMsg);

			if (sendNewRet != 0) {
				printf("NVMe: Failed to send check message: %d", sendNewRet);
				fflush(stdout);

				delete[] filterOptions.whiteListTypes;

				return 1;
			}

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

		const int getRet = send_horizonos_message(nvmePort, 1, &getMsg);

		if (getRet != 0) {
			printf("NVMe: Failed to send get message: %d", getRet);
			fflush(stdout);

			return 1;
		}

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
			printf("NVMe: Failed to get PCI port: %d!", srvRegisterResult);
			fflush(stdout);

			delete[] filterOptions.whiteListTypes;

			return 1;
		}

		printf("NVMe: PCI info: Port: %lu, TID: %u, Version: %u.%u.%u.", getResData.port, getResData.tid, getResData.versionMajor, getResData.versionMinor, getResData.versionPatch);
		fflush(stdout);

		pciPort = getResData.port;
		pciTid = getResData.tid;

		delete[] filterOptions.whiteListTypes;
	}

	vector<PciDevice> nvmeDevices {};

	{
		printf("NVMe: Requesting PCI service to search for NVMe devices...");
		fflush(stdout);

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

		const int sendSearchRet = send_horizonos_message(nvmePort, pciPort, &searchMsg);

		if (sendSearchRet != 0) {
			printf("NVMe: Failed to send PCI search message: %d!", sendSearchRet);
			fflush(stdout);

			return 1;
		}

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
			printf("NVMe: Failed to receive PCI search start message!");
			fflush(stdout);

			return 1;
		}

		printf("NVMe: PCI service will send %lu NVMe device(s).", getStartSearchData);
		fflush(stdout);

		for (uint64_t i = 0; i < getStartSearchData; ++i) {
			const int msgResult = receive_horizonos_message(nvmePort, &recvSearchMsg, &filterOptions);

			if (msgResult != 0) {
				printf("NVMe: Failed to receive PCI search device message!");
				fflush(stdout);

				return 1;
			}

			nvmeDevices.push_back(getSearchData);
		}

		printf("NVMe: Received %zu NVMe device(s) from PCI service.", nvmeDevices.size());
		fflush(stdout);

		delete[] startFilterOptions.whiteListTypes;
		delete[] filterOptions.whiteListTypes;
	}

	vector<NvmeDriver> controllerDrivers {};

	{
		if (nvmeDevices.empty()) {
			printf("NVMe: No nvme devices found, exiting.");
			fflush(stdout);

			return 2;
		}

		for (auto& dev : nvmeDevices) {
		    // 1. Read BAR0 physical address
		    uint32_t bar0lo = pciRead32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x10);
		    bool is64bit = ((bar0lo & 0x6u) == 0x4u);

		    // Read bar0hi ONCE — reused for both barPhys and post-probe restore
		    uint32_t bar0hi = is64bit
		        ? pciRead32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x14)
		        : 0u;

		    uint64_t barPhys = is64bit
		        ? (static_cast<uint64_t>(bar0hi) << 32) | (bar0lo & ~0xFu)
		        : (bar0lo & ~0xFu);

		    // 2. Discover BAR size — disable Memory Space during probe
		    uint32_t origCmd = pciRead32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x04);
		    pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x04, origCmd & ~(1u << 1));

		    // Write all 1s to both halves
		    pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x10, 0xFFFFFFFF);
		    if (is64bit) {
		    	pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x14, 0xFFFFFFFF);
		    }

		    // Read back size mask
		    uint32_t sizeLo = pciRead32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x10) & ~0xFu;
		    uint32_t sizeHi = is64bit
		        ? pciRead32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x14)
		        : 0u;

		    // Restore original BAR values
		    pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x10, bar0lo);
		    if (is64bit) {
		    	pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x14, bar0hi);
		    }

		    // Compute size
			uint64_t sizeMask;

			if (is64bit) {
				sizeMask = (static_cast<uint64_t>(sizeHi) << 32) | (sizeLo & ~0xFu);
			} else {
				sizeMask = sizeLo & ~0xFu;
			}

			uint64_t barSize = (sizeMask != 0) ? (~sizeMask + 1) : 0x4000;

			if (barSize == 0 || barSize > 0x10000000) { // sanity: max 256 MB
				printf("NVMe: Unreasonable BAR size 0x%lx, skipping.", barSize);

				continue;
			}

		    printf("NVMe: barPhys: 0x%lx, barSize: 0x%lx", barPhys, barSize);
		    fflush(stdout);

		    // 3. Enable Bus Master + Memory Space
		    pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x04, origCmd | (1u << 1) | (1u << 2));

		    // 4. Map MMIO
		    uint64_t mmioVirt = 0;
		    if (mmap_phys(barPhys, barSize, &mmioVirt, false) != 0) {
		    	pciWrite32(nvmePort, pciPort, dev.bus, dev.device, dev.function, 0x04, origCmd);

		        printf("NVMe: Failed to map BAR0 for %02x:%02x.%x, skipping.", dev.bus, dev.device, dev.function);
		        fflush(stdout);

		        continue;
		    }

		    // 5. Attach and initialize
		    NvmeDriver driver {};

		    driver.attachRegisters(reinterpret_cast<uint64_t *>(mmioVirt), barSize, &dev);

		    if (!driver.resetController()) {
		        printf("NVMe: Reset failed for %02x:%02x.%x, skipping.", dev.bus, dev.device, dev.function);
		        fflush(stdout);

		        continue;
		    }

		    if (!driver.initializeAdminQueues()) {
		        printf("NVMe: Admin queue init failed for %02x:%02x.%x, skipping.", dev.bus, dev.device, dev.function);
		        fflush(stdout);

		        continue;
		    }

		    if (!driver.enableController()) {
		        printf("NVMe: Enable controller failed for %02x:%02x.%x, skipping.", dev.bus, dev.device, dev.function);
		        fflush(stdout);

		        continue;
		    }

			if (!driver.identifyController()) {
				printf("NVMe: Identify controller failed for %02x:%02x.%x, skipping.", dev.bus, dev.device, dev.function);
				fflush(stdout);

				continue;
			}

			// Identify all namespaces reported by the controller
			// (controllerInfo.nn is now populated after identifyController)
			for (uint32_t nsid = 1; nsid <= driver.getNamespaceCount(); ++nsid) {
				driver.identifyNamespace(nsid);
			}

		    printf("NVMe: Controller %02x:%02x.%x ready.", dev.bus, dev.device, dev.function);
		    fflush(stdout);

		    controllerDrivers.push_back(driver);
		}
	}

	printf("NVMe: %zu controller(s) initialized.", controllerDrivers.size());
	fflush(stdout);

	vector<pthread_t> coreThreads {};

	/*{
		uint64_t cpuCount = 0;

		const int getErr = getCpuCount(&cpuCount);

		if (getErr != 0 or cpuCount == 0) {
			printf("NVMe: No CPUs found: %d!", getErr);
			fflush(stdout);

			return 1;
		}

		auto cpuIds = new uint64_t[cpuCount];

		const int getIDsErr = getCpuIds(cpuIds, cpuCount);

		if (getIDsErr != 0) {
			printf("NVMe: Error getting Cpu IDs: %d!", getIDsErr);
			fflush(stdout);

			return 1;
		}

		printf("NVMe: CPUs: %lu", cpuCount);
		fflush(stdout);

		pthread_attr_t threadAttr;

		pthread_attr_init(&threadAttr);
		pthread_attr_setstacksize(&threadAttr, 0x4000); // 16 KB stack

		for (uint64_t i = 0; i < cpuCount; ++i) {
			printf("NVMe: Cpu %lu with ID %ld", i, cpuIds[i]);
			fflush(stdout);

			auto *coreStruct = new CoreStruct();

			coreStruct->cpuId = cpuIds[i];
			coreStruct->nvmeDevices = &nvmeDevices;
			coreStruct->controllerDrivers = &controllerDrivers;

			pthread_t coreThread;

			const int coreResult = pthread_create(&coreThread, &threadAttr, NvmeDriver::coreHandler, coreStruct);

			if (coreResult != 0) {
				printf("NVMe: Failed to create core handler thread for core: %lu!", cpuIds[i]);
				fflush(stdout);

				return 1;
			}

			pthread_detach(coreThread);

			coreThreads.push_back(coreThread);
		}

		pthread_attr_destroy(&threadAttr);
	}*/

	for (;;) {}

	return 0;
}