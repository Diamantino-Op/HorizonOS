#include "Xhci.hpp"

#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

namespace {
	uint64_t xhciPort = 0;
	uint64_t pciPort = 0;

	void fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());
		memcpy(dst, name.data(), copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto mmioRead8(const uint64_t base, const uint32_t offset) -> uint8_t {
		return *reinterpret_cast<volatile uint8_t *>(base + offset);
	}

	auto mmioRead16(const uint64_t base, const uint32_t offset) -> uint16_t {
		return *reinterpret_cast<volatile uint16_t *>(base + offset);
	}

	auto mmioRead32(const uint64_t base, const uint32_t offset) -> uint32_t {
		return *reinterpret_cast<volatile uint32_t *>(base + offset);
	}

	void mmioWrite32(const uint64_t base, const uint32_t offset, const uint32_t value) {
		*reinterpret_cast<volatile uint32_t *>(base + offset) = value;
	}

	auto registerWithNameRegistry() -> bool {
		auto data = RegisterMsgData();
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());
		fillName(data.name, sizeof(data.name), data.nameLength, "XHCI");

		auto msg = hos_msg();
		msg.type = REGISTER_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(xhciPort, 1, &msg) != 0) {
			return false;
		}

		auto reply = RegisterReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { REPLY_REGISTER_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
	}

	auto waitForService(const char *name) -> GetReplyMsgData {
		for (;;) {
			auto check = CheckMsgData();
			fillName(check.name, sizeof(check.name), check.nameLength, name);

			auto msg = hos_msg();
			msg.type = CHECK_MSG_TYPE;
			msg.port = 1;
			msg.buffer = &check;
			msg.length = sizeof(check);

			send_horizonos_message(xhciPort, 1, &msg);

			auto reply = CheckReplyMsgData();
			auto recv = hos_msg();
			recv.buffer = &reply;
			recv.length = sizeof(reply);

			auto filter = filter_options();
			filter.whiteListTypes = new uint64_t[1] { REPLY_CHECK_MSG_TYPE };
			filter.whiteListCount = 1;

			const int ret = receive_horizonos_message(xhciPort, &recv, &filter);
			delete[] filter.whiteListTypes;

			if (ret == 0 and reply.exists) {
				break;
			}

			usleep(10000);
		}

		auto get = GetMsgData();
		fillName(get.name, sizeof(get.name), get.nameLength, name);

		auto msg = hos_msg();
		msg.type = GET_MSG_TYPE;
		msg.port = 1;
		msg.buffer = &get;
		msg.length = sizeof(get);

		send_horizonos_message(xhciPort, 1, &msg);

		auto reply = GetReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { REPLY_GET_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return reply;
	}

	auto pciRead32(const PciDevice &dev, const uint16_t offset) -> uint32_t {
		auto data = PciReadMsgData();
		data.bus = dev.bus;
		data.dev = dev.device;
		data.func = dev.function;
		data.offset = offset;
		data.width = 32;

		auto msg = hos_msg();
		msg.type = PCI_READ_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		send_horizonos_message(xhciPort, pciPort, &msg);

		auto reply = PciReadReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { PCI_READ_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return reply.data;
	}

	void pciWrite32(const PciDevice &dev, const uint16_t offset, const uint32_t value) {
		auto data = PciWriteMsgData();
		data.bus = dev.bus;
		data.dev = dev.device;
		data.func = dev.function;
		data.offset = offset;
		data.width = 32;
		data.data = value;

		auto msg = hos_msg();
		msg.type = PCI_WRITE_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		send_horizonos_message(xhciPort, pciPort, &msg);

		auto recv = hos_msg();
		recv.length = 0;

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { PCI_WRITE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;
	}

	auto findControllers() -> vector<PciDevice> {
		auto search = PciSearchDeviceMsgData();
		search.pciClass = PCI_CLASS_SERIAL_BUS;
		search.pciSubclass = PCI_SUBCLASS_USB;
		search.pciProg = PCI_PROGIF_XHCI;

		auto msg = hos_msg();
		msg.type = PCI_SEARCH_DEVICE_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &search;
		msg.length = sizeof(search);

		if (send_horizonos_message(xhciPort, pciPort, &msg) != 0) {
			return {};
		}

		uint64_t count = 0;
		auto start = hos_msg();
		start.buffer = &count;
		start.length = sizeof(count);

		auto startFilter = filter_options();
		startFilter.whiteListTypes = new uint64_t[1] { PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE };
		startFilter.whiteListCount = 1;

		if (receive_horizonos_message(xhciPort, &start, &startFilter) != 0) {
			delete[] startFilter.whiteListTypes;
			return {};
		}

		delete[] startFilter.whiteListTypes;

		vector<PciDevice> devices;
		devices.reserve(count);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { PCI_SEARCH_DEVICE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		for (uint64_t i = 0; i < count; ++i) {
			auto device = PciDevice();
			auto recv = hos_msg();
			recv.buffer = &device;
			recv.length = sizeof(device);

			if (receive_horizonos_message(xhciPort, &recv, &filter) != 0) {
				continue;
			}

			devices.push_back(device);
		}

		delete[] filter.whiteListTypes;

		return devices;
	}

	auto mapBar0(const PciDevice &dev, MappedController &controller) -> bool {
		const uint32_t bar0Lo = pciRead32(dev, PCI_BAR0);

		if ((bar0Lo & 0x1U) != 0) {
			printf("XHCI: BAR0 for %02x:%02x.%x is I/O space, skipping.", dev.bus, dev.device, dev.function);
			fflush(stdout);
			return false;
		}

		const bool is64Bit = (bar0Lo & 0x6U) == 0x4U;
		const uint32_t bar0Hi = is64Bit ? pciRead32(dev, PCI_BAR0 + 4) : 0;
		const uint64_t barPhys = is64Bit
			? (static_cast<uint64_t>(bar0Hi) << 32) | (bar0Lo & ~0xFULL)
			: (bar0Lo & ~0xFULL);

		const uint32_t originalCommand = pciRead32(dev, PCI_COMMAND);
		pciWrite32(dev, PCI_COMMAND, originalCommand & ~PCI_COMMAND_MEMORY_SPACE);
		pciWrite32(dev, PCI_BAR0, 0xFFFFFFFF);

		if (is64Bit) {
			pciWrite32(dev, PCI_BAR0 + 4, 0xFFFFFFFF);
		}

		const uint32_t sizeLo = pciRead32(dev, PCI_BAR0) & ~0xFU;
		const uint32_t sizeHi = is64Bit ? pciRead32(dev, PCI_BAR0 + 4) : 0;

		pciWrite32(dev, PCI_BAR0, bar0Lo);

		if (is64Bit) {
			pciWrite32(dev, PCI_BAR0 + 4, bar0Hi);
		}

		const uint64_t sizeMask = is64Bit
			? (static_cast<uint64_t>(sizeHi) << 32) | sizeLo
			: sizeLo;
		uint64_t barSize = sizeMask != 0 ? (~sizeMask + 1) : 0x10000;

		if (barSize == 0 or barSize > 0x10000000) {
			printf("XHCI: Unreasonable BAR0 size 0x%lx for %02x:%02x.%x.", barSize, dev.bus, dev.device, dev.function);
			fflush(stdout);
			pciWrite32(dev, PCI_COMMAND, originalCommand);
			return false;
		}

		if (barSize < 0x1000) {
			barSize = 0x1000;
		}

		pciWrite32(dev, PCI_COMMAND, originalCommand | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

		uint64_t mmioVirt = 0;

		if (mmap_phys(barPhys, barSize, &mmioVirt, false, MMapCacheMode::MAP_CACHE_UC) != 0) {
			printf("XHCI: Failed to map BAR0 phys=0x%lx size=0x%lx for %02x:%02x.%x.", barPhys, barSize, dev.bus, dev.device, dev.function);
			fflush(stdout);
			pciWrite32(dev, PCI_COMMAND, originalCommand);
			return false;
		}

		controller.pci = dev;
		controller.barPhys = barPhys;
		controller.barSize = barSize;
		controller.mmioVirt = mmioVirt;
		controller.originalCommand = originalCommand;

		return true;
	}

	auto waitForControllerReady(const uint64_t operationalBase) -> bool {
		for (int i = 0; i < 10000; ++i) {
			if ((mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_CNR) == 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto haltController(const uint64_t operationalBase) -> bool {
		uint32_t command = mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command &= ~XHCI_USBCMD_RUN;
		mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH) != 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto resetController(const uint64_t operationalBase) -> bool {
		uint32_t command = mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command |= XHCI_USBCMD_HCRST;
		mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((mmioRead32(operationalBase, XHCI_OP_USBCMD) & XHCI_USBCMD_HCRST) == 0) {
				return waitForControllerReady(operationalBase);
			}

			usleep(1000);
		}

		return false;
	}

	auto bringUpController(const MappedController &controller, const size_t index) -> bool {
		const uint64_t base = controller.mmioVirt;
		const uint8_t capLength = mmioRead8(base, XHCI_CAP_CAPLENGTH);
		const uint16_t version = mmioRead16(base, 0x02);
		const uint32_t hcsParams1 = mmioRead32(base, XHCI_CAP_HCSPARAMS1);
		const uint32_t hccParams1 = mmioRead32(base, XHCI_CAP_HCCPARAMS1);
		const uint32_t doorbellOffset = mmioRead32(base, XHCI_CAP_DBOFF) & ~0x3U;
		const uint32_t runtimeOffset = mmioRead32(base, XHCI_CAP_RTSOFF) & ~0x1FU;

		if (capLength == 0 or doorbellOffset == 0 or runtimeOffset == 0) {
			printf("XHCI: Controller %zu has invalid register layout cap=%u db=0x%x rt=0x%x.", index, capLength, doorbellOffset, runtimeOffset);
			fflush(stdout);
			return false;
		}

		const uint64_t operationalBase = base + capLength;
		const uint32_t maxSlots = hcsParams1 & 0xFFU;
		const uint32_t maxInterrupters = (hcsParams1 >> 8) & 0x7FFU;
		const uint32_t maxPorts = (hcsParams1 >> 24) & 0xFFU;
		const uint32_t pageSizeMask = mmioRead32(operationalBase, XHCI_OP_PAGESIZE);

		printf("XHCI: Controller %zu %02x:%02x.%x BAR=0x%lx size=0x%lx version=%x.%02x slots=%u ports=%u interrupters=%u pageMask=0x%x hcc=0x%x.",
		       index,
		       controller.pci.bus,
		       controller.pci.device,
		       controller.pci.function,
		       controller.barPhys,
		       controller.barSize,
		       version >> 8,
		       version & 0xFF,
		       maxSlots,
		       maxPorts,
		       maxInterrupters,
		       pageSizeMask,
		       hccParams1);
		fflush(stdout);

		if (!haltController(operationalBase)) {
			printf("XHCI: Controller %zu did not halt.", index);
			fflush(stdout);
			return false;
		}

		if (!resetController(operationalBase)) {
			printf("XHCI: Controller %zu reset timed out.", index);
			fflush(stdout);
			return false;
		}

		const uint32_t configSlots = min<uint32_t>(maxSlots, 32);
		mmioWrite32(operationalBase, XHCI_OP_CONFIG, configSlots);

		printf("XHCI: Controller %zu reset complete, configured %u device slot(s).", index, configSlots);
		fflush(stdout);

		return true;
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	if (register_horizonos_port(reinterpret_cast<long *>(&xhciPort)) != 0 or xhciPort == 0) {
		printf("XHCI: Failed to register port.");
		fflush(stdout);
		return 1;
	}

	printf("XHCI: Successfully registered port!");
	fflush(stdout);

	if (!registerWithNameRegistry()) {
		printf("XHCI: Failed to register service.");
		fflush(stdout);
		return 1;
	}

	printf("XHCI: Successfully registered service!");
	fflush(stdout);

	const GetReplyMsgData pciInfo = waitForService("PCI");
	pciPort = pciInfo.port;

	printf("XHCI: PCI info: Port: %lu, TID: %u, Version: %u.%u.%u.", pciInfo.port, pciInfo.tid, pciInfo.versionMajor, pciInfo.versionMinor, pciInfo.versionPatch);
	fflush(stdout);

	const vector<PciDevice> devices = findControllers();

	printf("XHCI: Received %zu xHCI device(s) from PCI service.", devices.size());
	fflush(stdout);

	if (devices.empty()) {
		printf("XHCI: No xHCI controllers found, exiting.");
		fflush(stdout);
		return 2;
	}

	vector<MappedController> controllers;
	controllers.reserve(devices.size());

	for (const auto &device : devices) {
		MappedController controller {};

		if (!mapBar0(device, controller)) {
			continue;
		}

		if (!bringUpController(controller, controllers.size())) {
			munmap_extra(reinterpret_cast<void *>(controller.mmioVirt), controller.barSize, false);
			pciWrite32(device, PCI_COMMAND, controller.originalCommand);
			
			continue;
		}

		controllers.push_back(controller);
	}

	printf("XHCI: %zu controller(s) initialized.", controllers.size());
	fflush(stdout);

	for (;;) {
		usleep(1000000);
	}
}
