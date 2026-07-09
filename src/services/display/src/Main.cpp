#include <abi-bits/hos_msg.h>
#include <horizonos/display.h>
#include <horizonos/generic.h>
#include <horizonos/syscall.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {
	constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
	constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
	constexpr uint64_t NAME_REGISTRY_PORT = 1;

	struct RegisterMsgData {
		uint16_t ownerPid {};
		uint16_t tid {};
		char name[16] {};
		size_t nameLength {};
		uint16_t versionMajor {};
		uint16_t versionMinor {};
		uint16_t versionPatch {};
	};

	struct RegisterReplyMsgData {
		bool success {};
	};

	uint64_t displayPort = 0;
	HorizonFramebufferInfo framebuffer {};
	uint8_t *frontBuffer = nullptr;
	uint8_t *backBuffer = nullptr;
	size_t backBufferSize = 0;

	void fillName(char *dst, const size_t dstSize, size_t &length, const char *name) {
		const size_t copyLen = std::min(dstSize - 1, strlen(name));
		memcpy(dst, name, copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	bool registerService(const char *name) {
		RegisterMsgData data {};
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());
		data.versionMajor = 0;
		data.versionMinor = 1;
		data.versionPatch = 0;
		fillName(data.name, sizeof(data.name), data.nameLength, name);

		hos_msg msg {};
		msg.type = REGISTER_MSG_TYPE;
		msg.port = NAME_REGISTRY_PORT;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(displayPort, NAME_REGISTRY_PORT, &msg) != 0) {
			return false;
		}

		RegisterReplyMsgData reply {};
		hos_msg recv {};
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		uint64_t type = REPLY_REGISTER_MSG_TYPE;
		filter_options filter {};
		filter.whiteListTypes = &type;
		filter.whiteListCount = 1;

		return receive_horizonos_message(displayPort, &recv, &filter) == 0 && reply.success;
	}

	bool initFramebuffer() {
		if (syscall(SYSCALL_GET_FRAMEBUFFER_INFO, nullptr, reinterpret_cast<uint64_t>(&framebuffer), 0) != 0) {
			return false;
		}

		uint64_t mapped = 0;

		if (mmap_phys(framebuffer.physicalAddress, framebuffer.length, &mapped, false, MAP_CACHE_WC) != 0) {
			return false;
		}

		frontBuffer = reinterpret_cast<uint8_t *>(mapped);
		backBufferSize = framebuffer.length;
		backBuffer = static_cast<uint8_t *>(aligned_alloc(64, (backBufferSize + 63) & ~static_cast<size_t>(63)));

		if (backBuffer == nullptr) {
			return false;
		}

		memset(backBuffer, 0, backBufferSize);
		memset(frontBuffer, 0, framebuffer.length);

		return true;
	}

	void copyRectToFront(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height) {
		const size_t bytesPerPixel = framebuffer.bpp / 8;
		const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;

		for (uint32_t row = 0; row < height; ++row) {
			const size_t offset = static_cast<size_t>(y + row) * framebuffer.pitch + static_cast<size_t>(x) * bytesPerPixel;
			memcpy(frontBuffer + offset, backBuffer + offset, rowBytes);
		}
	}

	HosDisplayPresentReply presentRect(const void *payload, const size_t payloadLength) {
		HosDisplayPresentReply reply {};

		if (payload == nullptr || payloadLength < sizeof(HosDisplayRectHeader) || framebuffer.bpp != 32) {
			return reply;
		}

		const auto *header = static_cast<const HosDisplayRectHeader *>(payload);
		const auto *pixels = reinterpret_cast<const uint8_t *>(header + 1);

		if (header->pixelBytes > payloadLength - sizeof(HosDisplayRectHeader)) {
			return reply;
		}

		if (header->x >= framebuffer.width || header->y >= framebuffer.height) {
			return reply;
		}

		const uint32_t width = std::min(header->width, framebuffer.width - header->x);
		const uint32_t height = std::min(header->height, framebuffer.height - header->y);
		const size_t bytesPerPixel = framebuffer.bpp / 8;
		const size_t minSrcPitch = static_cast<size_t>(header->width) * bytesPerPixel;

		if (width == 0 || height == 0 || header->srcPitch < minSrcPitch) {
			return reply;
		}

		const size_t requiredBytes = static_cast<size_t>(height - 1) * header->srcPitch + static_cast<size_t>(width) * bytesPerPixel;

		if (requiredBytes > header->pixelBytes) {
			return reply;
		}

		for (uint32_t row = 0; row < height; ++row) {
			const size_t dstOffset = static_cast<size_t>(header->y + row) * framebuffer.pitch + static_cast<size_t>(header->x) * bytesPerPixel;
			memcpy(backBuffer + dstOffset, pixels + static_cast<size_t>(row) * header->srcPitch, static_cast<size_t>(width) * bytesPerPixel);
		}

		copyRectToFront(header->x, header->y, width, height);

		reply.success = true;
		reply.clippedWidth = width;
		reply.clippedHeight = height;

		return reply;
	}

	void clearDisplay(const uint32_t color) {
		auto *back = reinterpret_cast<uint32_t *>(backBuffer);
		const size_t pixelsPerRow = framebuffer.pitch / sizeof(uint32_t);

		for (uint32_t y = 0; y < framebuffer.height; ++y) {
			for (uint32_t x = 0; x < framebuffer.width; ++x) {
				back[static_cast<size_t>(y) * pixelsPerRow + x] = color;
			}
		}

		copyRectToFront(0, 0, framebuffer.width, framebuffer.height);
	}

	void sendReply(const uint64_t destination, const uint64_t type, void *buffer, const size_t length) {
		hos_msg reply {};
		reply.type = type;
		reply.port = destination;
		reply.buffer = buffer;
		reply.length = length;
		send_horizonos_message(displayPort, destination, &reply);
	}
}

int main() {
	if (register_horizonos_port(reinterpret_cast<long *>(&displayPort)) != 0 || displayPort == 0) {
		return 1;
	}

	if (!initFramebuffer()) {
		printf("Display: framebuffer init failed\n");
		return 1;
	}

	if (!registerService("Display")) {
		printf("Display: failed to register service\n");
		return 1;
	}

	printf("Display: %ux%u pitch=%u bpp=%u\n", framebuffer.width, framebuffer.height, framebuffer.pitch, framebuffer.bpp);

	clearDisplay(0x00101010);

	constexpr size_t recvSize = sizeof(HosDisplayRectHeader) + HOS_DISPLAY_MAX_TRANSFER_BYTES;
	auto *recvBuffer = static_cast<uint8_t *>(malloc(recvSize));

	if (recvBuffer == nullptr) {
		return 1;
	}

	for (;;) {
		hos_msg msg {};
		msg.buffer = recvBuffer;
		msg.length = recvSize;

		if (receive_horizonos_message(displayPort, &msg, nullptr) != 0) {
			continue;
		}

		switch (msg.type) {
			case HOS_DISPLAY_GET_INFO: {
				HosDisplayInfoReply reply {};
				reply.success = true;
				reply.info.width = framebuffer.width;
				reply.info.height = framebuffer.height;
				reply.info.pitch = framebuffer.pitch;
				reply.info.bpp = framebuffer.bpp;
				reply.info.redMaskSize = framebuffer.redMaskSize;
				reply.info.redMaskShift = framebuffer.redMaskShift;
				reply.info.greenMaskSize = framebuffer.greenMaskSize;
				reply.info.greenMaskShift = framebuffer.greenMaskShift;
				reply.info.blueMaskSize = framebuffer.blueMaskSize;
				reply.info.blueMaskShift = framebuffer.blueMaskShift;
				sendReply(msg.src_port, HOS_DISPLAY_GET_INFO_REPLY, &reply, sizeof(reply));
				break;
			}

			case HOS_DISPLAY_PRESENT_RECT: {
				HosDisplayPresentReply reply = presentRect(recvBuffer, static_cast<size_t>(msg.ret_length));
				sendReply(msg.src_port, HOS_DISPLAY_PRESENT_RECT_REPLY, &reply, sizeof(reply));
				break;
			}

			case HOS_DISPLAY_CLEAR: {
				HosDisplayClearReply reply {};

				if (msg.ret_length >= static_cast<long>(sizeof(HosDisplayClearRequest))) {
					const auto *request = reinterpret_cast<const HosDisplayClearRequest *>(recvBuffer);
					clearDisplay(request->color);
					reply.success = true;
				}

				sendReply(msg.src_port, HOS_DISPLAY_CLEAR_REPLY, &reply, sizeof(reply));
				break;
			}

			default:
				break;
		}
	}
}
