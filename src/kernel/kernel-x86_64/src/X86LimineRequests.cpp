#define LIMINE_API_REVISION 3

#include "limine.h"

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebufferRequest = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_hhdm_request hhdmRequest = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_paging_mode_request pagingModeRequest = {
	.id = LIMINE_PAGING_MODE_REQUEST,
	.revision = 0,
	.response = nullptr,
	.mode = 1,
	.max_mode = 1,
	.min_mode = 0,
};