#include "horizonos/generic.h"
#include "uacpi/event.h"
#include "uacpi/sleep.h"
#include "uacpi/status.h"
#include "uacpi/utilities.h"

#include <cstdio>

uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx);

[[noreturn]] int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	if (const uacpi_status ret = uacpi_initialize(0); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize: %s", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_namespace_load(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to load namespaces: %s", uacpi_status_to_string(ret));
	}

	long mode = 0;

	get_irq_mode(&mode);

	if (const uacpi_status ret = uacpi_set_interrupt_model(static_cast<uacpi_interrupt_model>(mode)); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to set interrupt model: %s", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_namespace_initialize(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize namespaces: %s", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_finalize_gpe_initialization(); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to initialize GPEs: %s", uacpi_status_to_string(ret));
	}

	if (const uacpi_status ret = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, &handlerPowerBtn, nullptr); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to install pwr button handler: %s", uacpi_status_to_string(ret));
	}

	for (;;) {}

	return 0;
}

// TODO: Maybe move to syscall
uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx) {
	if (const uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to prepare for S5: %s\o{33}[0m", uacpi_status_to_string(ret));

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	printf("\o{33}[0;34muACPI: \o{33}[0;37mPreparing to enter S5...\o{33}[0m");

	//this->disableInts();

	printf("\o{33}[0;34muACPI: \o{33}[0;37mEntering S5...\o{33}[0m");

	if (const uacpi_status ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
		printf("\o{33}[0;31muACPI: \o{33}[0;37mFailed to enter S5: %s\o{33}[0m", uacpi_status_to_string(ret));

		return UACPI_INTERRUPT_NOT_HANDLED;
	}

	return UACPI_INTERRUPT_HANDLED;
}