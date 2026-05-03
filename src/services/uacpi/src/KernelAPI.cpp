#define UACPI_NATIVE_ALLOC_ZEROED

#include "uacpi/kernel_api.h"

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *outRsdpAddress) {

}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {

}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {

}

void *uacpi_kernel_alloc(uacpi_size size) {

}

void *uacpi_kernel_alloc_zeroed(uacpi_size size) {

}

void uacpi_kernel_free(void *mem) {

}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {


	switch (level) {
		case UACPI_LOG_ERROR:
			//terminal->printfUAcpi(false, "[    \o{33}[0;31merror    \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_WARN:
			//terminal->printfUAcpi(false, "[   \o{33}[0;33mwarning   \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_INFO:
			//terminal->printfUAcpi(false, "[ \o{33}[1;34minformation \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_TRACE:
		case UACPI_LOG_DEBUG:
			//terminal->printfUAcpi(false, "[    \o{33}[0;32mdebug    \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;
	}
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot() {

}

void uacpi_kernel_stall(uacpi_u8 uSec) {

}

// PCI

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

// IO

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {

}

void uacpi_kernel_io_unmap(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {

}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {

}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {

}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {

}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {

}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {

}

// Threads

void uacpi_kernel_sleep(uacpi_u64 mSec) {

}

uacpi_thread_id uacpi_kernel_get_thread_id() {

}

uacpi_handle uacpi_kernel_create_mutex() {

}

void uacpi_kernel_free_mutex(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {

}

void uacpi_kernel_release_mutex(uacpi_handle handle) {

}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {

}

uacpi_handle uacpi_kernel_create_event() {

}

void uacpi_kernel_free_event(uacpi_handle handle) {

}

void uacpi_kernel_signal_event(uacpi_handle handle) {

}

void uacpi_kernel_reset_event(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type workType, uacpi_work_handler workHandler, uacpi_handle ctx) {

}

uacpi_status uacpi_kernel_wait_for_work_completion() {

}

// Interrupts

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *request) {

}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler intHandler, uacpi_handle ctx, uacpi_handle *out_irq_handle) {

}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler intHandler, uacpi_handle irq_handle) {

}

uacpi_handle uacpi_kernel_create_spinlock() {

}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {

}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {

}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags prevIF) {

}

uacpi_interrupt_state uacpi_kernel_disable_interrupts() {

}

void uacpi_kernel_restore_interrupts(const uacpi_interrupt_state state) {

}