#include "UacpiService.hpp"

#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "pthread.h"
#include "uacpi/acpi.h"
#include "uacpi/context.h"
#include "uacpi/event.h"
#include "uacpi/internal/types.h"
#include "uacpi/sleep.h"
#include "uacpi/status.h"
#include "uacpi/tables.h"
#include "uacpi/utilities.h"
#include "unistd.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/io.h>

uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx);

using namespace std;

uint64_t uacpiPort = 0;
uint64_t pciPort = 0;
uint64_t pciTid = 0;
bool mcfgReady = false;

extern void *handleIrqs(void *);

auto UacpiService::start() -> int {
	const int registerResult = register_horizonos_port(reinterpret_cast<long *>(&uacpiPort));

	if (registerResult == 0) {
		printf("uACPI: Successfully registered port!");
		fflush(stdout);
	} else {
		printf("uACPI: Failed to register port: %d", registerResult);
		fflush(stdout);

		return 1;
	}

	{
		// Send

		auto newMsg = hos_msg();

		auto registerData = RegisterMsgData();

		registerData.ownerPid = getpid();
		registerData.tid = static_cast<uint16_t>(gettid());
		strncpy(registerData.name, string("uAcpi").c_str(), sizeof(registerData.name) - 1);
		registerData.name[sizeof(registerData.name) - 1] = '\0';
		registerData.nameLength = strlen(registerData.name) + 1;

		newMsg.type = REGISTER_MSG_TYPE;
		newMsg.port = 1;
		newMsg.buffer = &registerData;
		newMsg.length = sizeof(RegisterMsgData);

		send_horizonos_message(uacpiPort, 1, &newMsg);

		// Receive

		auto recvMsg = hos_msg();

		auto registerResData = RegisterReplyMsgData();

		recvMsg.buffer = &registerResData;
		recvMsg.length = sizeof(RegisterReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_REGISTER_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(uacpiPort, &recvMsg, &filterOptions);

		if (srvRegisterResult == 0 and registerResData.success) {
			printf("uACPI: Successfully registered service!");
			fflush(stdout);
		} else {
			printf("uACPI: Failed to register service: %d", srvRegisterResult);
			fflush(stdout);

			delete[] filterOptions.whiteListTypes;

			return 1;
		}

		delete[] filterOptions.whiteListTypes;
	}

	{
		auto checkMsg = hos_msg();

		auto checkData = CheckMsgData();

		strncpy(checkData.name, string("PCI").c_str(), sizeof(checkData.name) - 1);
		checkData.name[sizeof(checkData.name) - 1] = '\0';
		checkData.nameLength = strlen(checkData.name) + 1;

		checkMsg.type = CHECK_MSG_TYPE;
		checkMsg.port = 1;
		checkMsg.buffer = &checkData;
		checkMsg.length = sizeof(CheckMsgData);

		auto recvCheckMsg = hos_msg();

		auto checkResData = CheckReplyMsgData();

		recvCheckMsg.buffer = &checkResData;
		recvCheckMsg.length = sizeof(CheckReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_CHECK_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		for (;;) {
			send_horizonos_message(uacpiPort, 1, &checkMsg);

			const int srvRegisterResult = receive_horizonos_message(uacpiPort, &recvCheckMsg, &filterOptions);

			if (srvRegisterResult == 0 and checkResData.exists) {
				break;
			}

			usleep(10000);
		}

		delete[] filterOptions.whiteListTypes;
	}

	{
		auto getMsg = hos_msg();

		auto getData = GetMsgData();

		strncpy(getData.name, string("PCI").c_str(), sizeof(getData.name) - 1);
		getData.name[sizeof(getData.name) - 1] = '\0';
		getData.nameLength = strlen(getData.name) + 1;

		getMsg.type = GET_MSG_TYPE;
		getMsg.port = 1;
		getMsg.buffer = &getData;
		getMsg.length = sizeof(GetMsgData);

		send_horizonos_message(uacpiPort, 1, &getMsg);

		auto recvGetMsg = hos_msg();

		auto getResData = GetReplyMsgData();

		recvGetMsg.buffer = &getResData;
		recvGetMsg.length = sizeof(GetReplyMsgData);

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ REPLY_GET_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		const int srvRegisterResult = receive_horizonos_message(uacpiPort, &recvGetMsg, &filterOptions);

		delete[] filterOptions.whiteListTypes;

		if (srvRegisterResult != 0) {
			printf("uACPI: Failed to get PCI port!");
			fflush(stdout);

			return 1;
		}

		printf("uACPI: PCI info: Port: %lu, TID: %u, Version: %u.%u.%u.", getResData.port, getResData.tid, getResData.versionMajor, getResData.versionMinor, getResData.versionPatch);
		fflush(stdout);

		pciPort = getResData.port;
		pciTid = getResData.tid;
	}

	pthread_t irqHandlerThread;

	const int irqHandlerThreadResult = pthread_create(&irqHandlerThread, nullptr, handleIrqs, nullptr);

	if (irqHandlerThreadResult != 0) {
		printf("uACPI: Failed to create irq handler thread!");
		fflush(stdout);

		return 1;
	}

	pthread_detach(irqHandlerThread);

	if (ioperm(PCI_CONFIG_ADDRESS, 8, 1) != 0) {
		printf("uACPI: Failed to acquire PCI config I/O permissions!");
		fflush(stdout);

		return 1;
	}

	if (const uacpi_status ret = uacpi_initialize(0); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	if (const uacpi_status ret = uacpi_namespace_load(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to load namespaces: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	UacpiUtils::processMcfg();

	long mode = 0;

	get_irq_mode(&mode);

	if (const uacpi_status ret = uacpi_set_interrupt_model(static_cast<uacpi_interrupt_model>(mode)); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to set interrupt model: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	if (const uacpi_status ret = uacpi_namespace_initialize(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize namespaces: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	if (const uacpi_status ret = uacpi_finalize_gpe_initialization(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize GPEs: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	if (const uacpi_status ret = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, &handlerPowerBtn, nullptr); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to install pwr button handler: %s", uacpi_status_to_string(ret));
		fflush(stdout);
	}

	for (;;) {}

	return 0;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	UacpiService service;

	return service.start();
}

// TODO: Maybe move to syscall
uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx) {
	if (const uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to prepare for S5: %s", uacpi_status_to_string(ret));
		fflush(stdout);

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	printf("\o{33}[0;34muACPI: \o{33}[0;37mPreparing to enter S5...");
	fflush(stdout);

	//this->disableInts();
	set_int_status(false);

	printf("\o{33}[0;34muACPI: \o{33}[0;37mEntering S5...");
	fflush(stdout);

	if (const uacpi_status ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to enter S5: %s", uacpi_status_to_string(ret));
		fflush(stdout);

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	return UACPI_INTERRUPT_HANDLED;
}

static uacpi_iteration_decision pciRootCallback(void *user, uacpi_namespace_node *node, uint32_t /*depth*/) {
	(void)user;

    uint64_t seg = 0, bbn = 0;
    uacpi_eval_simple_integer(node, "_SEG", &seg);
    uacpi_eval_simple_integer(node, "_BBN", &bbn);

    // Try _CBA first — some firmware exposes ECAM base directly on the node
    uint64_t ecamBase = 0;
    uacpi_eval_simple_integer(node, "_CBA", &ecamBase);

	uint8_t endBus = 255;

    // If _CBA absent, find it in the MCFG table by matching seg + startBus
    if (ecamBase == 0) {
        uacpi_table mcfgTable;

        if (uacpi_table_find_by_signature("MCFG", &mcfgTable) == UACPI_STATUS_OK) {
            const auto *mcfg   = static_cast<const acpi_mcfg *>(mcfgTable.ptr);
            const size_t count = (mcfg->hdr.length - sizeof(acpi_mcfg)) / sizeof(acpi_mcfg_allocation);
            const auto *allocs = reinterpret_cast<const acpi_mcfg_allocation *>(mcfg + 1);

            for (size_t i = 0; i < count; ++i) {
                if (allocs[i].segment == static_cast<uint16_t>(seg) && allocs[i].start_bus <= static_cast<uint8_t>(bbn) && allocs[i].end_bus >= static_cast<uint8_t>(bbn)) {
                    // ECAM base for this specific bus within the segment
                    ecamBase = allocs[i].address + (bbn << 20);
                	endBus = allocs[i].end_bus;

                    break;
                }
            }

            uacpi_table_unref(&mcfgTable);
        }
    }

    printf("\033[0;34muACPI: \033[0;37mPCI root bridge: seg=%llu bus=%llu ecam=%llx", static_cast<unsigned long long>(seg), static_cast<unsigned long long>(bbn), static_cast<unsigned long long>(ecamBase));
	fflush(stdout);

    // Send one mcfg_segment message per root bridge found
    auto segMsg = hos_msg();

	auto segData = McfgSegmentMsgData();

	segData.ecamBase = ecamBase;
	segData.segment = seg;
	segData.bbn = bbn;
	segData.endBus = endBus;

	segMsg.type   = MCFG_SEGMENT_MSG_TYPE;
    segMsg.port   = pciPort;
    segMsg.buffer = &segData;
    segMsg.length = sizeof(McfgSegmentMsgData);

    send_horizonos_message(uacpiPort, pciPort, &segMsg);

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void UacpiUtils::processMcfg() {
	if (pciPort == 0) {
		printf("uACPI: Failed to get PCI port!");
		fflush(stdout);

		for (;;) {
			usleep(10000);
		}
	}

	{
		// Wait for pci_ready from the PCI service (port 3 → port 2).
		auto waitMsg = hos_msg();

		waitMsg.length = 0;

		auto filterOptions = filter_options();

		filterOptions.whiteListTypes = new uint64_t[1]{ PCI_READY_MSG_TYPE };
		filterOptions.whiteListCount = 1;

		if (receive_horizonos_message(uacpiPort, &waitMsg, &filterOptions) == 0) {
			printf("\033[0;34muACPI: \033[0;37mPCI service ready, forwarding MCFG...");
			fflush(stdout);
		}

		delete[] filterOptions.whiteListTypes;

		const char *pciRootIds[] = { "PNP0A03", "PNP0A08", nullptr };

		uacpi_find_devices_at(uacpi_namespace_root(), pciRootIds, pciRootCallback, nullptr);

		// Signal PCI that all segments have been sent.
		auto doneMsg = hos_msg();

		doneMsg.type   = MCFG_DONE_MSG_TYPE;
		doneMsg.port   = pciPort;
		doneMsg.length = 0;

		send_horizonos_message(uacpiPort, pciPort, &doneMsg);

		mcfgReady = true;
	}
}
