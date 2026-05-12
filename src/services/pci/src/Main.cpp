#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "Pci.hpp"

#include <cstdio>
#include <string>
#include <array>
#include <vector>
#include <thread>
#include <pthread.h>
#include <unistd.h>

using namespace std;

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t PCI_READY_MSG_TYPE = 0x10;

constexpr uint64_t MCFG_DONE_MSG_TYPE = 0x100;
constexpr uint64_t MCFG_SEGMENT_MSG_TYPE = 0x200;

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

struct McfgSegmentMsgData {
	uint64_t ecamBase {};
	uint64_t segment {};
	uint64_t bbn {};
	uint8_t endBus {};
};

uint64_t pciPort = 0;
uint64_t uacpiPort = 0;
uint64_t uacpiTid = 0;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	// ── 1. Register port ───────────────────────────────────────────────────
    if (const int r = register_horizonos_port(reinterpret_cast<long *>(&pciPort)); r != 0) {
        printf("PCI: Failed to register port: %d\n", r);

        return 1;
    }

    printf("PCI: Successfully registered port!\n");

    // ── 2. Register with name-registry ───────────────────────────────────────
	{
    	// Send

    	auto *newMsg = new hos_msg();

    	auto *registerData = new RegisterMsgData();

    	registerData->ownerPid = getpid();
    	registerData->tid = hash<thread::id>{}(this_thread::get_id());
    	strncpy(registerData->name, string("PCI").c_str(), sizeof(registerData->name) - 1);
    	registerData->name[sizeof(registerData->name) - 1] = '\0';
    	registerData->nameLength = strlen(registerData->name) + 1;

    	newMsg->type = REGISTER_MSG_TYPE;
    	newMsg->port = 1;
    	newMsg->buffer = registerData;
    	newMsg->length = sizeof(RegisterMsgData);

    	send_horizonos_message(pciPort, 1, newMsg);

    	delete newMsg;
    	delete registerData;

    	// Receive

    	auto *recvMsg = new hos_msg();

    	auto *registerResData = new RegisterReplyMsgData();

    	recvMsg->buffer = registerResData;
    	recvMsg->length = sizeof(RegisterReplyMsgData);

    	auto *filterOptions = new filter_options();

    	filterOptions->whiteListTypes = new uint64_t[1]{ REPLY_REGISTER_MSG_TYPE };
    	filterOptions->whiteListCount = 1;

    	const int srvRegisterResult = receive_horizonos_message(pciPort, recvMsg, filterOptions);

    	if (srvRegisterResult == 0 and registerResData->success) {
    		printf("PCI: Successfully registered service!\n");
    	} else {
    		printf("PCI: Failed to register service: %d\n", srvRegisterResult);

    		delete recvMsg;
    		delete registerResData;

    		delete filterOptions;

    		return 1;
    	}

    	delete recvMsg;
    	delete registerResData;

    	delete filterOptions;
	}

	// ── 3. Get uAcpi port from name-registry ─────────────────────────────────
    {
		// Send

		auto *checkMsg = new hos_msg();

		auto *checkData = new CheckMsgData();

		strncpy(checkData->name, string("uAcpi").c_str(), sizeof(checkData->name) - 1);
		checkData->name[sizeof(checkData->name) - 1] = '\0';
		checkData->nameLength = strlen(checkData->name) + 1;

		checkMsg->type = CHECK_MSG_TYPE;
		checkMsg->port = 1;
		checkMsg->buffer = checkData;
		checkMsg->length = sizeof(CheckMsgData);

		// Reply

		auto *recvCheckMsg = new hos_msg();

		auto *checkResData = new CheckReplyMsgData();

		recvCheckMsg->buffer = checkResData;
		recvCheckMsg->length = sizeof(CheckReplyMsgData);

		auto *filterOptions = new filter_options();

		filterOptions->whiteListTypes = new uint64_t[1]{ REPLY_CHECK_MSG_TYPE };
		filterOptions->whiteListCount = 1;

		for (;;) {
			send_horizonos_message(pciPort, 1, checkMsg);

			const int srvRegisterResult = receive_horizonos_message(pciPort, recvCheckMsg, filterOptions);

			if (srvRegisterResult == 0 and checkResData->exists) {
				break;
			}
		}

		delete checkMsg;
		delete checkData;

		delete recvCheckMsg;
		delete checkResData;

		delete filterOptions;
	}

	{
		// Send

		auto *getMsg = new hos_msg();

		auto *getData = new GetMsgData();

		strncpy(getData->name, string("uAcpi").c_str(), sizeof(getData->name) - 1);
		getData->name[sizeof(getData->name) - 1] = '\0';
		getData->nameLength = strlen(getData->name) + 1;

		getMsg->type = GET_MSG_TYPE;
		getMsg->port = 1;
		getMsg->buffer = getData;
		getMsg->length = sizeof(GetMsgData);

		send_horizonos_message(pciPort, 1, getMsg);

		// Reply

		auto *recvGetMsg = new hos_msg();

		auto *getResData = new GetReplyMsgData();

		recvGetMsg->buffer = getResData;
		recvGetMsg->length = sizeof(GetReplyMsgData);

		auto *filterOptions = new filter_options();

		filterOptions->whiteListTypes = new uint64_t[1]{ REPLY_GET_MSG_TYPE };
		filterOptions->whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(pciPort, recvGetMsg, filterOptions);

		if (srvRegisterResult != 0) {
			printf("PCI: Failed to get uACPI port!\n");

			delete getMsg;
			delete getData;

			delete recvGetMsg;
			delete getResData;

			delete filterOptions;

			return 1;
		}

		printf("PCI: uAcpi info: Port: %lu, TID: %u, Version: %u.%u.%u.\n", getResData->port, getResData->tid, getResData->versionMajor, getResData->versionMinor, getResData->versionPatch);

		uacpiPort = getResData->port;
		uacpiTid = getResData->tid;

		delete getMsg;
		delete getData;

		delete recvGetMsg;
		delete getResData;

		delete filterOptions;
	}

    // ── 4. Notify uACPI that PCI is ready (triggers MCFG forwarding) ─────────
    {
    	auto *notifyMsg = new hos_msg();

    	notifyMsg->type = PCI_READY_MSG_TYPE;
    	notifyMsg->port = uacpiPort;
    	notifyMsg->length = 0;

    	send_horizonos_message(pciPort, uacpiPort, notifyMsg);

    	delete notifyMsg;
    }

    printf("PCI: Notified uACPI (port %lu), waiting for MCFG data...\n", uacpiPort);

    // ── 5. Receive mcfg_segment messages from uACPI ───────────────────────────
    // uACPI sends one "mcfg_segment;..." per entry, then "mcfg_done".
    {
        for (;;) {
            auto *msg = new hos_msg();

        	auto *mcfgData = new McfgSegmentMsgData();

            msg->buffer = mcfgData;
            msg->length = sizeof(McfgSegmentMsgData);

        	auto *filterOptions = new filter_options();

        	filterOptions->whiteListTypes = new uint64_t[1]{ MCFG_SEGMENT_MSG_TYPE };
        	filterOptions->whiteListCount = 1;

            if (receive_horizonos_message(pciPort, msg, filterOptions) != 0) {
                delete msg;
            	delete mcfgData;

                continue;
            }

            delete msg;
        	delete mcfgData;

            if (notice == "mcfg_done") {
                printf("PCI: All MCFG segments received (%zu segment(s))\n", g_ecamSegments.size());

                break;
            }

            if (notice.starts_with("mcfg_segment;")) {
                // Parse inline without going through the message loop.
                // Format: "mcfg_segment;<physBase>;<seg>;<startBus>;<endBus>"
                vector<string> parts;
                size_t start = 0;

                while (start <= notice.size()) {
                    const size_t sep = notice.find(';', start);

                    if (sep == string::npos) {
                        parts.emplace_back(notice.substr(start));

                        break;
                    }

                    parts.emplace_back(notice.substr(start, sep - start));
                    start = sep + 1;
                }

                if (parts.size() >= 5) {
                    addEcamSegment(stoull(parts[1]), static_cast<uint16_t>(stoul(parts[2])), static_cast<uint8_t>(stoul(parts[3])), static_cast<uint8_t>(stoul(parts[4])));
                }
            }
        }
    }

    // ── 6. Enumerate and log all devices ─────────────────────────────────────
    printf("PCI: Enumerating devices...\n");

    vector<PciDevice> devices;
    enumeratePci(devices);

    printf("PCI: Found %zu device(s):\n", devices.size());

    for (const auto &d : devices) {
        printf("  [%04x:%02x:%02x.%x]  Vendor=%04x  Device=%04x  "
               "Class=%02x:%02x  ProgIF=%02x  %s\n",
               0,           // segment group (extend later if needed)
               d.bus, d.device, d.function,
               d.vendorId, d.deviceId,
               d.classCode, d.subclass,
               d.progIf,
               d.isPcie ? "(PCIe/ECAM)" : "(PCI/legacy)");
    }

    // ── 7. Start the message-handling thread ──────────────────────────────────
    pthread_t msgThread;

    if (pthread_create(&msgThread, nullptr, pciMessageLoop, nullptr) != 0) {
        printf("PCI: Failed to create message loop thread\n");

        return 1;
    }

    pthread_detach(msgThread);

    // ── 8. Main thread idle loop ──────────────────────────────────────────────
    for (;;) {
        usleep(1000000);
    }

    return 0;
}