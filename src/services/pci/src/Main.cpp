#include "Pci.hpp"
#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "sys/io.h"

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <vector>

using namespace std;

uint64_t pciPort = 0;
uint64_t uacpiPort = 0;
uint64_t uacpiTid = 0;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	// ── 1. Register port ───────────────────────────────────────────────────
    if (const int r = register_horizonos_port(reinterpret_cast<long *>(&pciPort)); r != 0) {
        printf("PCI: Failed to register port: %d", r);
    	fflush(stdout);

        return 1;
    }

    printf("PCI: Successfully registered port!");
	fflush(stdout);

    // ── 2. Register with name-registry ───────────────────────────────────────
	{
    	// Send

    	auto newMsg = hos_msg();

    	auto registerData = RegisterMsgData();

    	registerData.ownerPid = getpid();
		registerData.tid = static_cast<uint16_t>(gettid());
    	strncpy(registerData.name, string("PCI").c_str(), sizeof(registerData.name) - 1);
    	registerData.name[sizeof(registerData.name) - 1] = '\0';
    	registerData.nameLength = strlen(registerData.name) + 1;

    	newMsg.type = REGISTER_MSG_TYPE;
    	newMsg.port = 1;
    	newMsg.buffer = &registerData;
    	newMsg.length = sizeof(RegisterMsgData);

    	send_horizonos_message(pciPort, 1, &newMsg);

    	// Receive

    	auto recvMsg = hos_msg();

    	auto registerResData = RegisterReplyMsgData();

    	recvMsg.buffer = &registerResData;
    	recvMsg.length = sizeof(RegisterReplyMsgData);

    	auto filterOptions = filter_options();

    	filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_REGISTER_MSG_TYPE };
    	filterOptions.whiteListCount = 1;

    	const int srvRegisterResult = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

    	if (srvRegisterResult == 0 and registerResData.success) {
    		printf("PCI: Successfully registered service!");
    		fflush(stdout);
    	} else {
    		printf("PCI: Failed to register service: %d", srvRegisterResult);
    		fflush(stdout);

    		delete[] filterOptions.whiteListTypes;

    		return 1;
    	}

    	delete[] filterOptions.whiteListTypes;
	}

	// ── 3. Get uAcpi port from name-registry ─────────────────────────────────
    {
		// Send

		auto checkMsg = hos_msg();

		auto checkData = CheckMsgData();

		strncpy(checkData.name, string("uAcpi").c_str(), sizeof(checkData.name) - 1);
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
			send_horizonos_message(pciPort, 1, &checkMsg);

			const int srvRegisterResult = receive_horizonos_message(pciPort, &recvCheckMsg, &filterOptions);

			if (srvRegisterResult != 0 and checkMsg.type != REPLY_CHECK_MSG_TYPE) {
				printf("PCI: Received unexpected message type while checking for uACPI: %lu", recvCheckMsg.type);
				fflush(stdout);
			}

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

		strncpy(getData.name, string("uAcpi").c_str(), sizeof(getData.name) - 1);
		getData.name[sizeof(getData.name) - 1] = '\0';
		getData.nameLength = strlen(getData.name) + 1;

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &getData;
		getMsg.length = sizeof(GetMsgData);

		send_horizonos_message(pciPort, 1, &getMsg);

		// Reply

		auto recvGetMsg = hos_msg();

		auto getResData = GetReplyMsgData();

		recvGetMsg.buffer = &getResData;
		recvGetMsg.length = sizeof(GetReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_GET_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(pciPort, &recvGetMsg, &filterOptions);

		if (srvRegisterResult != 0) {
			printf("PCI: Failed to get uACPI port!");
			fflush(stdout);

			delete[] filterOptions.whiteListTypes;

			return 1;
		}

    	if (recvGetMsg.type != REPLY_GET_MSG_TYPE) {
    		printf("PCI: Received unexpected message type while getting uACPI port: %lu", recvGetMsg.type);
    		fflush(stdout);
    	}

		printf("PCI: uAcpi info: Port: %lu, TID: %u, Version: %u.%u.%u.", getResData.port, getResData.tid, getResData.versionMajor, getResData.versionMinor, getResData.versionPatch);
    	fflush(stdout);

		uacpiPort = getResData.port;
		uacpiTid = getResData.tid;

    	delete[] filterOptions.whiteListTypes;
	}

    // ── 4. Notify uACPI that PCI is ready (triggers MCFG forwarding) ─────────
    {
    	auto notifyMsg = hos_msg();

    	notifyMsg.type = PCI_READY_MSG_TYPE;
    	notifyMsg.port = uacpiPort;
    	notifyMsg.length = 0;

    	send_horizonos_message(pciPort, uacpiPort, &notifyMsg);
    }

    printf("PCI: Notified uACPI (port %lu), waiting for MCFG data...", uacpiPort);
	fflush(stdout);

    // ── 5. Receive mcfg_segment messages from uACPI ───────────────────────────
    {
        for (;;) {
            auto msg = hos_msg();

        	auto mcfgData = McfgSegmentMsgData();

            msg.buffer = &mcfgData;
            msg.length = sizeof(McfgSegmentMsgData);

        	auto filterOptions = filter_options();

        	filterOptions.whiteListTypes = new uint64_t[2]{ MCFG_SEGMENT_MSG_TYPE, MCFG_DONE_MSG_TYPE };
        	filterOptions.whiteListCount = 2;

            if (receive_horizonos_message(pciPort, &msg, &filterOptions) != 0) {
            	delete[] filterOptions.whiteListTypes;

                continue;
            }

        	delete[] filterOptions.whiteListTypes;

            if (msg.type == MCFG_DONE_MSG_TYPE) {
                printf("PCI: All MCFG segments received (%zu segment(s))", g_ecamSegments.size());
            	fflush(stdout);

                break;
            }

            if (msg.type == MCFG_SEGMENT_MSG_TYPE) {
            	addEcamSegment(mcfgData.ecamBase, mcfgData.segment, mcfgData.bbn, mcfgData.endBus);
            }
        }
    }

    // ── 6. Enumerate and log all devices ─────────────────────────────────────
    printf("PCI: Enumerating devices...");
	fflush(stdout);

    vector<PciDevice> devices;
    enumeratePci(devices);

    printf("PCI: Found %zu device(s):", devices.size());
	fflush(stdout);

    for (const auto &d : devices) {
        printf("  [%04x:%02x:%02x.%x]  Vendor=%04x  Device=%04x  "
               "Class=%02x:%02x  ProgIF=%02x  %s",
               0,           // segment group (extend later if needed)
               d.bus, d.device, d.function,
               d.vendorId, d.deviceId,
               d.classCode, d.subclass,
               d.progIf,
               d.isPcie ? "(PCIe/ECAM)" : "(PCI/legacy)");
    	fflush(stdout);
    }

    // ── 7. Start the message-handling thread ──────────────────────────────────
	// Keep legacy I/O mapped permanently for fallback reads/writes.
	if (ioperm(PCI_CONFIG_ADDRESS, 8, 1) != 0) {
		printf("PCI: Failed to acquire I/O permissions in message loop");
		fflush(stdout);

		return 1;
	}

	pthread_t pciSearchDeviceThread;

	if (pthread_create(&pciSearchDeviceThread, nullptr, handleSearchDevice, &devices) != 0) {
		printf("PCI: Failed to create pci search device message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciSearchDeviceThread);

    pthread_t pciReadThread;

    if (pthread_create(&pciReadThread, nullptr, handlePciRead, nullptr) != 0) {
        printf("PCI: Failed to create pci read message loop thread");
    	fflush(stdout);

        return 1;
    }

    pthread_detach(pciReadThread);

	pthread_t pciWriteThread;

	if (pthread_create(&pciWriteThread, nullptr, handlePciWrite, nullptr) != 0) {
		printf("PCI: Failed to create pci write message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciWriteThread);

	pthread_t pciMsiAllocThread;

	if (pthread_create(&pciMsiAllocThread, nullptr, handleMsiAlloc, nullptr) != 0) {
		printf("PCI: Failed to create pci msi alloc message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsiAllocThread);

	pthread_t pciMsiFreeThread;

	if (pthread_create(&pciMsiFreeThread, nullptr, handleMsiFree, nullptr) != 0) {
		printf("PCI: Failed to create pci msi free message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsiFreeThread);

	pthread_t pciMsixAllocThread;

	if (pthread_create(&pciMsixAllocThread, nullptr, handleMsixAlloc, nullptr) != 0) {
		printf("PCI: Failed to create pci msix alloc message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsixAllocThread);

	pthread_t pciMsixFreeThread;

	if (pthread_create(&pciMsixFreeThread, nullptr, handleMsixFree, nullptr) != 0) {
		printf("PCI: Failed to create pci msix free message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsixFreeThread);

	pthread_t pciMsixGlobalEnableThread;

	if (pthread_create(&pciMsixGlobalEnableThread, nullptr, handleMsixGlobalEnable, nullptr) != 0) {
		printf("PCI: Failed to create pci msix gobal enable message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsixGlobalEnableThread);

	pthread_t pciMsixGlobalDisableThread;

	if (pthread_create(&pciMsixGlobalDisableThread, nullptr, handleMsixGlobalDisable, nullptr) != 0) {
		printf("PCI: Failed to create pci msix gobal disable message loop thread");
		fflush(stdout);

		return 1;
	}

	pthread_detach(pciMsixGlobalDisableThread);

    // ── 8. Main thread idle loop ──────────────────────────────────────────────
    for (;;) {
        usleep(1000000);
    }

    return 0;
}