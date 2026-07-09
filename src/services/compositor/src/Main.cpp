#include <abi-bits/hos_msg.h>
#include <horizonos/display.h>
#include <horizonos/generic.h>
#include <horizonos/window.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
	constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
	constexpr uint64_t GET_MSG_TYPE = 0x3;
	constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
	constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
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

	struct GetMsgData {
		char name[16] {};
		size_t nameLength {};
	};

	struct GetReplyMsgData {
		uint64_t port {};
		uint16_t tid {};
		uint16_t versionMajor {};
		uint16_t versionMinor {};
		uint16_t versionPatch {};
	};

	struct Window {
		uint64_t id {};
		uint32_t x {};
		uint32_t y {};
		uint32_t width {};
		uint32_t height {};
		std::string title;
		std::vector<uint32_t> pixels;
		bool visible {true};
	};

	uint64_t compositorPort = 0;
	uint64_t displayPort = 0;
	HosDisplayInfo displayInfo {};
	uint64_t nextWindowId = 1;
	std::vector<Window> windows;

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
		data.versionMinor = 1;
		fillName(data.name, sizeof(data.name), data.nameLength, name);

		hos_msg msg {};
		msg.type = REGISTER_MSG_TYPE;
		msg.port = NAME_REGISTRY_PORT;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(compositorPort, NAME_REGISTRY_PORT, &msg) != 0) {
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

		return receive_horizonos_message(compositorPort, &recv, &filter) == 0 && reply.success;
	}

	uint64_t waitForService(const char *name) {
		for (;;) {
			GetMsgData request {};
			fillName(request.name, sizeof(request.name), request.nameLength, name);

			hos_msg msg {};
			msg.type = GET_MSG_TYPE;
			msg.port = NAME_REGISTRY_PORT;
			msg.buffer = &request;
			msg.length = sizeof(request);

			if (send_horizonos_message(compositorPort, NAME_REGISTRY_PORT, &msg) == 0) {
				GetReplyMsgData reply {};
				hos_msg recv {};
				recv.buffer = &reply;
				recv.length = sizeof(reply);

				uint64_t type = REPLY_GET_MSG_TYPE;
				filter_options filter {};
				filter.whiteListTypes = &type;
				filter.whiteListCount = 1;

				if (receive_horizonos_message(compositorPort, &recv, &filter) == 0 && reply.port != 0) {
					return reply.port;
				}
			}

			usleep(10000);
		}
	}

	template <typename ReplyT>
	bool requestDisplay(const uint64_t type, void *request, const size_t requestLength, ReplyT &reply) {
		hos_msg msg {};
		msg.type = type;
		msg.port = displayPort;
		msg.buffer = request;
		msg.length = requestLength;

		if (send_horizonos_message(compositorPort, displayPort, &msg) != 0) {
			return false;
		}

		hos_msg recv {};
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		uint64_t replyType = type + 1;
		filter_options filter {};
		filter.whiteListTypes = &replyType;
		filter.whiteListCount = 1;

		return receive_horizonos_message(compositorPort, &recv, &filter) == 0;
	}

	Window *findWindow(const uint64_t id) {
		for (auto &window : windows) {
			if (window.id == id) {
				return &window;
			}
		}

		return nullptr;
	}

	uint32_t sampleCompositedPixel(const uint32_t screenX, const uint32_t screenY) {
		for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
			Window &window = *it;

			if (!window.visible || screenX < window.x || screenY < window.y || screenX >= window.x + window.width || screenY >= window.y + window.height) {
				continue;
			}

			const uint32_t localX = screenX - window.x;
			const uint32_t localY = screenY - window.y;

			return window.pixels[static_cast<size_t>(localY) * window.width + localX];
		}

		return 0x00101010;
	}

	bool sendDisplayRect(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height) {
		if (width == 0 || height == 0 || x >= displayInfo.width || y >= displayInfo.height) {
			return true;
		}

		const uint32_t clippedWidth = std::min(width, displayInfo.width - x);
		const uint32_t clippedHeight = std::min(height, displayInfo.height - y);
		const uint32_t bytesPerPixel = 4;
		const uint32_t maxRowsPerChunk = std::max<uint32_t>(1, HOS_DISPLAY_MAX_TRANSFER_BYTES / (clippedWidth * bytesPerPixel));
		std::vector<uint8_t> packet(sizeof(HosDisplayRectHeader) + static_cast<size_t>(maxRowsPerChunk) * clippedWidth * bytesPerPixel);

		for (uint32_t row = 0; row < clippedHeight;) {
			const uint32_t rows = std::min(maxRowsPerChunk, clippedHeight - row);
			auto *header = reinterpret_cast<HosDisplayRectHeader *>(packet.data());
			auto *pixels = reinterpret_cast<uint32_t *>(packet.data() + sizeof(HosDisplayRectHeader));

			header->x = x;
			header->y = y + row;
			header->width = clippedWidth;
			header->height = rows;
			header->srcPitch = clippedWidth * bytesPerPixel;
			header->pixelBytes = rows * header->srcPitch;

			for (uint32_t dy = 0; dy < rows; ++dy) {
				for (uint32_t dx = 0; dx < clippedWidth; ++dx) {
					pixels[static_cast<size_t>(dy) * clippedWidth + dx] = sampleCompositedPixel(x + dx, y + row + dy);
				}
			}

			HosDisplayPresentReply reply {};

			if (!requestDisplay(HOS_DISPLAY_PRESENT_RECT, packet.data(), sizeof(HosDisplayRectHeader) + header->pixelBytes, reply) || !reply.success) {
				return false;
			}

			row += rows;
		}

		return true;
	}

	void redrawWindowArea(const Window &window) {
		sendDisplayRect(window.x, window.y, window.width, window.height);
	}

	template <typename T>
	void sendReply(const uint64_t destination, const uint64_t type, T &reply) {
		hos_msg msg {};
		msg.type = type;
		msg.port = destination;
		msg.buffer = &reply;
		msg.length = sizeof(reply);
		send_horizonos_message(compositorPort, destination, &msg);
	}

	void handleCreate(const hos_msg &msg, const void *buffer) {
		HosWindowCreateReply reply {};

		if (msg.ret_length >= static_cast<long>(sizeof(HosWindowCreateRequest))) {
			const auto *request = static_cast<const HosWindowCreateRequest *>(buffer);
			const uint32_t width = std::min(request->width, displayInfo.width);
			const uint32_t height = std::min(request->height, displayInfo.height);

			if (width != 0 && height != 0) {
				Window window {};
				window.id = nextWindowId++;
				window.x = std::min(request->x, displayInfo.width - 1);
				window.y = std::min(request->y, displayInfo.height - 1);
				window.width = width;
				window.height = height;
				window.pixels.assign(static_cast<size_t>(width) * height, 0x00202020);

				if (request->titleLength > 0 && request->titleLength <= sizeof(request->title) && request->title[request->titleLength - 1] == '\0') {
					window.title.assign(request->title, request->titleLength - 1);
				}

				reply.success = true;
				reply.windowId = window.id;
				reply.width = width;
				reply.height = height;
				windows.push_back(std::move(window));
				redrawWindowArea(windows.back());
			}
		}

		sendReply(msg.src_port, HOS_WINDOW_CREATE_REPLY, reply);
	}

	void handleDrawRect(const hos_msg &msg, const void *buffer) {
		HosWindowDrawRectReply reply {};

		if (msg.ret_length >= static_cast<long>(sizeof(HosWindowDrawRectHeader))) {
			const auto *header = static_cast<const HosWindowDrawRectHeader *>(buffer);
			const auto *pixels = reinterpret_cast<const uint8_t *>(header + 1);
			Window *window = findWindow(header->windowId);

			if (window != nullptr && header->x < window->width && header->y < window->height && header->pixelBytes <= static_cast<uint32_t>(msg.ret_length - sizeof(HosWindowDrawRectHeader))) {
				const uint32_t width = std::min(header->width, window->width - header->x);
				const uint32_t height = std::min(header->height, window->height - header->y);
				const uint32_t rowBytes = width * sizeof(uint32_t);
				const size_t requiredBytes = height == 0 ? 0 : static_cast<size_t>(height - 1) * header->srcPitch + rowBytes;

				if (width != 0 && height != 0 && header->srcPitch >= header->width * sizeof(uint32_t) && requiredBytes <= header->pixelBytes) {
					for (uint32_t row = 0; row < height; ++row) {
						memcpy(
							&window->pixels[static_cast<size_t>(header->y + row) * window->width + header->x],
							pixels + static_cast<size_t>(row) * header->srcPitch,
							rowBytes);
					}

					reply.success = true;
					reply.clippedWidth = width;
					reply.clippedHeight = height;
				}
			}
		}

		sendReply(msg.src_port, HOS_WINDOW_DRAW_RECT_REPLY, reply);
	}

	void handlePresent(const hos_msg &msg, const void *buffer) {
		HosWindowPresentReply reply {};

		if (msg.ret_length >= static_cast<long>(sizeof(HosWindowPresentRequest))) {
			const auto *request = static_cast<const HosWindowPresentRequest *>(buffer);
			Window *window = findWindow(request->windowId);

			if (window != nullptr) {
				const uint32_t localX = std::min(request->x, window->width);
				const uint32_t localY = std::min(request->y, window->height);
				const uint32_t width = request->width == 0 ? window->width - localX : std::min(request->width, window->width - localX);
				const uint32_t height = request->height == 0 ? window->height - localY : std::min(request->height, window->height - localY);
				reply.success = sendDisplayRect(window->x + localX, window->y + localY, width, height);
			}
		}

		sendReply(msg.src_port, HOS_WINDOW_PRESENT_REPLY, reply);
	}

	void handleMove(const hos_msg &msg, const void *buffer) {
		HosWindowMoveReply reply {};

		if (msg.ret_length >= static_cast<long>(sizeof(HosWindowMoveRequest))) {
			const auto *request = static_cast<const HosWindowMoveRequest *>(buffer);
			Window *window = findWindow(request->windowId);

			if (window != nullptr) {
				const uint32_t oldX = window->x;
				const uint32_t oldY = window->y;
				window->x = std::min(request->x, displayInfo.width - 1);
				window->y = std::min(request->y, displayInfo.height - 1);
				sendDisplayRect(oldX, oldY, window->width, window->height);
				redrawWindowArea(*window);
				reply.success = true;
			}
		}

		sendReply(msg.src_port, HOS_WINDOW_MOVE_REPLY, reply);
	}

	void handleDestroy(const hos_msg &msg, const void *buffer) {
		HosWindowDestroyReply reply {};

		if (msg.ret_length >= static_cast<long>(sizeof(HosWindowDestroyRequest))) {
			const auto *request = static_cast<const HosWindowDestroyRequest *>(buffer);

			for (auto it = windows.begin(); it != windows.end(); ++it) {
				if (it->id != request->windowId) {
					continue;
				}

				const uint32_t x = it->x;
				const uint32_t y = it->y;
				const uint32_t width = it->width;
				const uint32_t height = it->height;
				windows.erase(it);
				sendDisplayRect(x, y, width, height);
				reply.success = true;
				break;
			}
		}

		sendReply(msg.src_port, HOS_WINDOW_DESTROY_REPLY, reply);
	}
}

int main() {
	if (register_horizonos_port(reinterpret_cast<long *>(&compositorPort)) != 0 || compositorPort == 0) {
		return 1;
	}

	displayPort = waitForService("Display");

	HosDisplayInfoReply infoReply {};
	if (!requestDisplay(HOS_DISPLAY_GET_INFO, nullptr, 0, infoReply) || !infoReply.success) {
		printf("Compositor: failed to query Display\n");
		return 1;
	}

	displayInfo = infoReply.info;

	if (!registerService("Compositor")) {
		printf("Compositor: failed to register service\n");
		return 1;
	}

	printf("Compositor: ready on %ux%u\n", displayInfo.width, displayInfo.height);

	constexpr size_t recvSize = sizeof(HosWindowDrawRectHeader) + HOS_WINDOW_MAX_TRANSFER_BYTES;
	std::vector<uint8_t> recvBuffer(recvSize);

	for (;;) {
		hos_msg msg {};
		msg.buffer = recvBuffer.data();
		msg.length = recvBuffer.size();

		if (receive_horizonos_message(compositorPort, &msg, nullptr) != 0) {
			continue;
		}

		switch (msg.type) {
			case HOS_WINDOW_CREATE:
				handleCreate(msg, recvBuffer.data());
				break;
			case HOS_WINDOW_DRAW_RECT:
				handleDrawRect(msg, recvBuffer.data());
				break;
			case HOS_WINDOW_PRESENT:
				handlePresent(msg, recvBuffer.data());
				break;
			case HOS_WINDOW_MOVE:
				handleMove(msg, recvBuffer.data());
				break;
			case HOS_WINDOW_DESTROY:
				handleDestroy(msg, recvBuffer.data());
				break;
			default:
				break;
		}
	}
}
