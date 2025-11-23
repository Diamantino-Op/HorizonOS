#include "limine.h"

#include "memory/VirtualMemory.hpp"

__attribute__((used, section(".limine_requests")))
volatile limine_executable_address_request kernelAddressRequest = {
	.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request memMapRequest = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_stack_size_request stackSizeRequest = {
	.id = LIMINE_STACK_SIZE_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
	.stack_size = kernel::common::memory::kernelStackSize
};

__attribute__((used, section(".limine_requests")))
volatile limine_rsdp_request rsdpRequest = {
	.id = LIMINE_RSDP_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_module_request moduleRequest = {
	.id = LIMINE_MODULE_REQUEST_ID,
	.revision = 0,
	.response =	nullptr,
	.internal_module_count = 0,
	.internal_modules = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebufferRequest = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_hhdm_request hhdmRequest = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_paging_mode_request pagingModeRequest = {
	.id = LIMINE_PAGING_MODE_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
	.mode = 0,
	.max_mode = 1,
	.min_mode = 0,
};

__attribute__((used, section(".limine_requests")))
volatile limine_mp_request mpRequest = {
	.id = LIMINE_MP_REQUEST_ID,
	.revision = 0,
	.response = nullptr,
	.flags = 0x1
};