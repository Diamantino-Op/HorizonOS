#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "thread"
#include "uacpi/event.h"
#include "uacpi/sleep.h"
#include "uacpi/status.h"
#include "uacpi/tables.h"
#include "uacpi/utilities.h"
#include "unistd.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx);

using namespace std;

void sendMcfg();

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	const int registerResult = register_horizonos_port(2);

	if (registerResult == 0) {
		printf("uACPI: Successfully registered port!\n");
	} else {
		printf("uACPI: Failed to register port: %d\n", registerResult);

		return 1;
	}

	auto *newMsg = new hos_msg();

	std::string msgStr = "register;" + to_string(getpid()) + ";" + to_string(hash<thread::id>{}(this_thread::get_id())) + ";uACPI;1;0;0";

	newMsg->port = 1;
	newMsg->buffer = static_cast<void *>(msgStr.data());
	newMsg->length = msgStr.size();

	send_horizonos_message(2, 1, newMsg);

	delete newMsg;

	array<char, 1024> receiveBuffer{};
	auto *recvMsg = new hos_msg();

	recvMsg->buffer = receiveBuffer.data();
	recvMsg->length = receiveBuffer.size();

	const int srvRegisterResult = receive_horizonos_message(2, recvMsg);

	const string retMsg(receiveBuffer.data(), static_cast<size_t>(recvMsg->ret_length));

	const int retVal = stoi(retMsg);

	delete recvMsg;

	if (srvRegisterResult == 0 and retVal == 1) {
		printf("uACPI: Successfully registered service!\n");
	} else {
		printf("uACPI: Failed to register service: %d\n", srvRegisterResult);

		return 1;
	}

	if (const uacpi_status ret = uacpi_initialize(0); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize: %s\n", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_namespace_load(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to load namespaces: %s\n", uacpi_status_to_string(ret));
	}

	sendMcfg();

	long mode = 0;

	get_irq_mode(&mode);

	if (const uacpi_status ret = uacpi_set_interrupt_model(static_cast<uacpi_interrupt_model>(mode)); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to set interrupt model: %s\n", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_namespace_initialize(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize namespaces: %s\n", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_finalize_gpe_initialization(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize GPEs: %s\n", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, &handlerPowerBtn, nullptr); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to install pwr button handler: %s\n", uacpi_status_to_string(ret));
	}

	for (;;) {}

	return 0;
}

// TODO: Maybe move to syscall
uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx) {
	if (const uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to prepare for S5: %s\n\o{33}[0m", uacpi_status_to_string(ret));

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	printf("\o{33}[0;34muACPI: \o{33}[0;37mPreparing to enter S5...\n\o{33}[0m");

	//this->disableInts();

	printf("\o{33}[0;34muACPI: \o{33}[0;37mEntering S5...\n\o{33}[0m");

	if (const uacpi_status ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to enter S5: %s\n\o{33}[0m", uacpi_status_to_string(ret));

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	return UACPI_INTERRUPT_HANDLED;
}

static uacpi_iteration_decision pciRootCallback(void *user, uacpi_namespace_node *node, uint32_t /*depth*/) {
	(void)user;

    uint64_t seg = 0, bbn = 0;
    uacpi_eval_integer(node, "_SEG", nullptr, &seg);
    uacpi_eval_integer(node, "_BBN", nullptr, &bbn);

    // Try _CBA first — some firmware exposes ECAM base directly on the node
    uint64_t ecamBase = 0;
    uacpi_eval_integer(node, "_CBA", nullptr, &ecamBase);

    // If _CBA absent, find it in the MCFG table by matching seg + startBus
    if (ecamBase == 0) {
        uacpi_table mcfgTable;

        if (uacpi_table_find_by_signature("MCFG", &mcfgTable) == UACPI_STATUS_OK) {
            const auto *mcfg   = static_cast<const acpi_mcfg *>(mcfgTable.ptr);
            const size_t count = (mcfg->hdr.length - sizeof(acpi_mcfg)) / sizeof(acpi_mcfg_allocation);
            const auto *allocs = reinterpret_cast<const acpi_mcfg_allocation *>(mcfg + 1);

            for (size_t i = 0; i < count; ++i) {
                if (allocs[i].segment   == static_cast<uint16_t>(seg) && allocs[i].start_bus <= static_cast<uint8_t>(bbn) && allocs[i].end_bus   >= static_cast<uint8_t>(bbn)) {
                    // ECAM base for this specific bus within the segment
                    ecamBase = allocs[i].address + (bbn << 20);

                    break;
                }
            }

            uacpi_table_unref(&mcfgTable);
        }
    }

    printf("\033[0;34muACPI: \033[0;37mPCI root bridge: seg=%llu bus=%llu ecam=%llx\n", static_cast<unsigned long long>(seg), static_cast<unsigned long long>(bbn), static_cast<unsigned long long>(ecamBase));

    // Send one mcfg_segment message per root bridge found
    auto *segMsg = new hos_msg();

    string segStr = "mcfg_segment;" + to_string(ecamBase) + ";" + to_string(seg) + ";" + to_string(bbn) + ";" + to_string(255);

    segMsg->port   = 3;
    segMsg->buffer = static_cast<void *>(segStr.data());
    segMsg->length = segStr.size();

    send_horizonos_message(2, 3, segMsg);

    delete segMsg;

    return UACPI_ITERATION_DECISION_CONTINUE;
}

void sendMcfg() {
	// Wait for pci_ready from the PCI service (port 3 → port 2).
	array<char, 256> buf{};
	auto *waitMsg = new hos_msg();

	waitMsg->buffer = buf.data();
	waitMsg->length = buf.size();

	while (true) {
		if (receive_horizonos_message(2, waitMsg) == 0 && waitMsg->ret_length > 0) {
			const string n(buf.data(), static_cast<size_t>(waitMsg->ret_length));

			if (n.starts_with("pci_ready;")) {
				printf("\033[0;34muACPI: \033[0;37mPCI service ready, forwarding MCFG...\n");

				break;
			}
		}
	}

	delete waitMsg;

	static const char *pciRootIds[] = { "PNP0A03", "PNP0A08", nullptr };

	uacpi_find_devices_at(uacpi_namespace_root(), pciRootIds, pciRootCallback, nullptr);

	// Signal PCI that all segments have been sent.
	auto *doneMsg = new hos_msg();

	const char *doneStr = "mcfg_done";

	doneMsg->port   = 3;
	doneMsg->buffer = const_cast<void *>(static_cast<const void *>(doneStr));
	doneMsg->length = strlen(doneStr);

	send_horizonos_message(2, 3, doneMsg);

	delete doneMsg;
}