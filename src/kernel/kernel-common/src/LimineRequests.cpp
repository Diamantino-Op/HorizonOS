#include "limine.h"

#include "memory/VirtualMemory.hpp"

__attribute__((used, section(".limine_requests")))
volatile limine_executable_address_request kernelAddressRequest = {
	.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request memMapRequest = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_stack_size_request stackSizeRequest = {
	.id = LIMINE_STACK_SIZE_REQUEST,
	.revision = 0,
	.response = nullptr,
	.stack_size = kernel::common::memory::kernelStackSize
};

__attribute__((used, section(".limine_requests")))
volatile limine_rsdp_request rsdpRequest = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0,
	.response = nullptr,
};