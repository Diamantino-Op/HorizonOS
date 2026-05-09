#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "thread"
#include "uacpi/event.h"
#include "uacpi/sleep.h"
#include "uacpi/status.h"
#include "uacpi/utilities.h"
#include "unistd.h"

#include <cstdio>
#include <string>

uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx);

using namespace std;

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