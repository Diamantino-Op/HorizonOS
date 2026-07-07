#include "Xhci.hpp"

#include "horizonos/generic.h"
#include "pthread.h"
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
	vector<pthread_t> eventThreads;

	void fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());
		memcpy(dst, name.data(), copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto mmioRead8(const uint64_t base, const uint32_t offset) -> uint8_t {
		return *reinterpret_cast<volatile uint8_t *>(base + offset);
	}

	auto mmioRead32(const uint64_t base, const uint32_t offset) -> uint32_t {
		return *reinterpret_cast<volatile uint32_t *>(base + offset);
	}

	void mmioWrite32(const uint64_t base, const uint32_t offset, const uint32_t value) {
		*reinterpret_cast<volatile uint32_t *>(base + offset) = value;
	}

	void mmioWrite64(const uint64_t base, const uint32_t offset, const uint64_t value) {
		*reinterpret_cast<volatile uint64_t *>(base + offset) = value;
	}

	auto allocatePage(AllocatedPage &page) -> bool {
		if (allocPhysPage(&page.phys) != 0) {
			return false;
		}

		if (mmap_phys(page.phys, XHCI_PAGE_SIZE, &page.virt, false) != 0) {
			freePhysPage(page.phys);
			page.phys = 0;
			return false;
		}

		memset(reinterpret_cast<void *>(page.virt), 0, XHCI_PAGE_SIZE);

		return true;
	}

	void freePage(AllocatedPage &page) {
		if (page.virt != 0) {
			munmap_extra(reinterpret_cast<void *>(page.virt), XHCI_PAGE_SIZE, false);
		}

		if (page.phys != 0) {
			freePhysPage(page.phys);
		}

		page = {};
	}

	void releaseControllerMemory(ControllerMemory &memory) {
		for (auto &scratchpad : memory.scratchpads) {
			freePage(scratchpad);
		}

		memory.scratchpads.clear();
		freePage(memory.scratchpadArray);
		freePage(memory.erst);
		freePage(memory.eventRing);
		freePage(memory.commandRing);
		freePage(memory.dcbaa);
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

	void msixGlobalEnable(const PciDevice &dev) {
		auto data = PciMsixGlobalEnableMsgData();
		data.bus = dev.bus;
		data.dev = dev.device;
		data.func = dev.function;

		auto msg = hos_msg();
		msg.type = PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		send_horizonos_message(xhciPort, pciPort, &msg);

		auto recv = hos_msg();
		recv.length = 0;

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { PCI_MSIX_GLOBAL_ENABLE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;
	}

	auto msixAllocVector(const PciDevice &dev, const uint16_t tableIndex, const uint64_t notifyPort, const uint64_t lapicId = 1000000) -> uint8_t {
		auto data = PciMsixAllocMsgData();
		data.bus = dev.bus;
		data.dev = dev.device;
		data.func = dev.function;
		data.idx = tableIndex;
		data.port = notifyPort;
		data.lapicId = lapicId;

		auto msg = hos_msg();
		msg.type = PCI_MSIX_ALLOC_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		send_horizonos_message(xhciPort, pciPort, &msg);

		auto reply = PciMsixAllocReplyMsgData();
		auto recv = hos_msg();
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { PCI_MSIX_ALLOC_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);
		delete[] filter.whiteListTypes;

		return reply.vec;
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

	auto startController(const uint64_t operationalBase, const bool interruptsEnabled) -> bool {
		uint32_t command = mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command |= XHCI_USBCMD_RUN;

		if (interruptsEnabled) {
			command |= XHCI_USBCMD_INTE;
		} else {
			command &= ~XHCI_USBCMD_INTE;
		}

		mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH) == 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	void setControllerInterruptsEnabled(const uint64_t operationalBase, const bool enabled) {
		uint32_t command = mmioRead32(operationalBase, XHCI_OP_USBCMD);

		if (enabled) {
			command |= XHCI_USBCMD_INTE;
		} else {
			command &= ~XHCI_USBCMD_INTE;
		}

		mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);
	}

	auto maxScratchpadBuffers(const uint32_t hcsParams2) -> uint32_t {
		return ((hcsParams2 >> 27) & 0x1FU) << 5U | ((hcsParams2 >> 21) & 0x1FU);
	}

	auto setupScratchpads(ControllerMemory &memory, const uint32_t maxScratchpads) -> bool {
		memory.maxScratchpads = maxScratchpads;

		if (maxScratchpads == 0) {
			return true;
		}

		if (!allocatePage(memory.scratchpadArray)) {
			return false;
		}

		auto *dcbaa = reinterpret_cast<uint64_t *>(memory.dcbaa.virt);
		auto *scratchpadArray = reinterpret_cast<uint64_t *>(memory.scratchpadArray.virt);
		dcbaa[0] = memory.scratchpadArray.phys;
		memory.scratchpads.reserve(maxScratchpads);

		for (uint32_t i = 0; i < maxScratchpads; ++i) {
			auto page = AllocatedPage();

			if (!allocatePage(page)) {
				return false;
			}

			scratchpadArray[i] = page.phys;
			memory.scratchpads.push_back(page);
		}

		return true;
	}

	auto setupCommandRing(ControllerMemory &memory, const uint64_t operationalBase) -> bool {
		if (!allocatePage(memory.commandRing)) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(memory.commandRing.virt);
		auto &link = ring[XHCI_COMMAND_RING_TRBS - 1];
		link.parameterLow = static_cast<uint32_t>(memory.commandRing.phys);
		link.parameterHigh = static_cast<uint32_t>(memory.commandRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		mmioWrite64(operationalBase, XHCI_OP_CRCR, memory.commandRing.phys | XHCI_TRB_CYCLE);

		return true;
	}

	auto setupEventRing(ControllerMemory &memory, const uint64_t runtimeBase) -> bool {
		if (!allocatePage(memory.eventRing)) {
			return false;
		}

		if (!allocatePage(memory.erst)) {
			return false;
		}

		auto *erst = reinterpret_cast<XhciErstEntry *>(memory.erst.virt);
		erst[0].ringSegmentBase = memory.eventRing.phys;
		erst[0].ringSegmentSize = XHCI_EVENT_RING_TRBS;

		const uint64_t interrupterBase = runtimeBase + 0x20;
		mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, 0);
		mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMOD, 0);
		mmioWrite32(interrupterBase, XHCI_INTERRUPTER_ERSTSZ, XHCI_ERST_ENTRIES);
		mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERSTBA, memory.erst.phys);
		mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERDP, memory.eventRing.phys | XHCI_ERDP_EHB);

		return true;
	}

	void setInterrupterEnabled(const MappedController &controller, const bool enabled) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;
		mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, enabled ? 0x3 : 0);
	}

	void acknowledgeEvents(const MappedController &controller) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;
		const uint32_t iman = mmioRead32(interrupterBase, XHCI_INTERRUPTER_IMAN);
		mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, (iman & 0x2U) | 0x1U);
		mmioWrite32(controller.operationalBase, XHCI_OP_USBSTS, XHCI_USBSTS_EINT);
	}

	void updateEventDequeuePointer(const MappedController &controller) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;
		const uint64_t eventDequeuePhys = controller.memory.eventRing.phys + controller.memory.eventDequeueIndex * sizeof(XhciTrb);
		mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERDP, eventDequeuePhys | XHCI_ERDP_EHB);
	}

	auto eventType(const XhciTrb &event) -> uint32_t {
		return (event.control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
	}

	auto completionCode(const XhciTrb &event) -> uint32_t {
		return (event.status >> 24) & 0xFFU;
	}

	void ringDoorbell(const MappedController &controller, const uint32_t target, const uint32_t value) {
		mmioWrite32(controller.doorbellBase, target * 4, value);
	}

	auto enqueueCommand(MappedController &controller, const XhciTrb &command) -> bool {
		if (controller.memory.commandEnqueueIndex >= XHCI_COMMAND_RING_TRBS - 1) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(controller.memory.commandRing.virt);
		auto &slot = ring[controller.memory.commandEnqueueIndex];
		slot = command;

		if (controller.memory.commandProducerCycle != 0) {
			slot.control |= XHCI_TRB_CYCLE;
		} else {
			slot.control &= ~XHCI_TRB_CYCLE;
		}

		++controller.memory.commandEnqueueIndex;

		if (controller.memory.commandEnqueueIndex == XHCI_COMMAND_RING_TRBS - 1) {
			controller.memory.commandEnqueueIndex = 0;
			controller.memory.commandProducerCycle ^= 1;
		}

		ringDoorbell(controller, 0, 0);

		return true;
	}

	auto submitNoopCommand(MappedController &controller) -> bool {
		auto command = XhciTrb();
		command.control = XHCI_TRB_TYPE_NOOP_COMMAND << XHCI_TRB_TYPE_SHIFT;

		return enqueueCommand(controller, command);
	}

	auto commandPhys(const MappedController &controller, const uint32_t index) -> uint64_t {
		return controller.memory.commandRing.phys + index * sizeof(XhciTrb);
	}

	auto enqueueCommandAndGetPhys(MappedController &controller, const XhciTrb &command, uint64_t &phys) -> bool {
		const uint32_t index = controller.memory.commandEnqueueIndex;

		if (!enqueueCommand(controller, command)) {
			return false;
		}

		phys = commandPhys(controller, index);

		return true;
	}

	auto waitForCommandCompletion(MappedController &controller, const uint64_t commandTrbPhys, XhciTrb &completion, const int timeoutMs = 1000) -> bool {
		uint32_t ignoredLogs = 32;

		for (int i = 0; i < timeoutMs; ++i) {
			auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);

			for (;;) {
				auto &event = events[controller.memory.eventDequeueIndex];

				if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
					break;
				}

				const uint32_t type = eventType(event);
				const uint64_t eventParam = static_cast<uint64_t>(event.parameterLow) | (static_cast<uint64_t>(event.parameterHigh) << 32);
				const bool matched = type == XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT and eventParam == commandTrbPhys;

				if (matched) {
					completion = event;
				} else if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
					const uint32_t portId = static_cast<uint32_t>(eventParam >> 24);
					printf("XHCI: Port status change event for port %u during command wait.", portId);
					fflush(stdout);
				} else if (ignoredLogs != 0) {
					printf("XHCI: Ignored event type=%u code=%u while waiting for command 0x%lx.", type, completionCode(event), commandTrbPhys);
					fflush(stdout);
					--ignoredLogs;
				}

				controller.memory.eventDequeueIndex++;

				if (controller.memory.eventDequeueIndex == XHCI_EVENT_RING_TRBS) {
					controller.memory.eventDequeueIndex = 0;
					controller.memory.eventConsumerCycle ^= 1;
				}

				updateEventDequeuePointer(controller);

				if (matched) {
					acknowledgeEvents(controller);
					return true;
				}
			}

			usleep(1000);
		}

		return false;
	}

	auto runCommand(MappedController &controller, const XhciTrb &command, XhciTrb &completion, const int timeoutMs = 1000) -> bool {
		uint64_t phys = 0;

		if (!enqueueCommandAndGetPhys(controller, command, phys)) {
			return false;
		}

		if (!waitForCommandCompletion(controller, phys, completion, timeoutMs)) {
			printf("XHCI: Timed out waiting for command TRB 0x%lx.", phys);
			fflush(stdout);
			return false;
		}

		const uint32_t code = completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS) {
			printf("XHCI: Command TRB 0x%lx failed code=%u ctrl=0x%x status=0x%x.", phys, code, completion.control, completion.status);
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto drainEvents(MappedController &controller, uint32_t &loggedEvents) -> uint32_t {
		uint32_t drained = 0;

		for (;;) {
			auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);
			auto &event = events[controller.memory.eventDequeueIndex];

			if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
				break;
			}

			const uint32_t type = eventType(event);
			const uint32_t code = completionCode(event);

			if (loggedEvents < 32) {
				printf("XHCI: Event ctrl=0x%x status=0x%x param=%08x%08x type=%u code=%u index=%u.",
				       event.control,
				       event.status,
				       event.parameterHigh,
				       event.parameterLow,
				       type,
				       code,
				       controller.memory.eventDequeueIndex);
				fflush(stdout);
				++loggedEvents;
			}

			controller.memory.eventDequeueIndex++;
			++drained;

			if (controller.memory.eventDequeueIndex == XHCI_EVENT_RING_TRBS) {
				controller.memory.eventDequeueIndex = 0;
				controller.memory.eventConsumerCycle ^= 1;
			}

			updateEventDequeuePointer(controller);
		}

		if (drained != 0) {
			acknowledgeEvents(controller);
		}

		return drained;
	}

	void logControllerStatus(const MappedController &controller, const char *phase) {
		const uint32_t usbcmd = mmioRead32(controller.operationalBase, XHCI_OP_USBCMD);
		const uint32_t usbsts = mmioRead32(controller.operationalBase, XHCI_OP_USBSTS);
		const uint32_t iman = mmioRead32(controller.runtimeBase + 0x20, XHCI_INTERRUPTER_IMAN);

		printf("XHCI: %s status USBCMD=0x%x USBSTS=0x%x IMAN=0x%x.", phase, usbcmd, usbsts, iman);
		fflush(stdout);
	}

	void logPorts(const MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portOffset = XHCI_OP_PORT_REGS + (port - 1) * XHCI_OP_PORT_STRIDE + XHCI_PORTSC;
			const uint32_t portsc = mmioRead32(controller.operationalBase, portOffset);

			if (portsc != 0) {
				printf("XHCI: Port %u PORTSC=0x%x.", port, portsc);
				fflush(stdout);
			}
		}
	}

	void postStartProbe(MappedController &controller) {
		uint32_t loggedEvents = 0;

		logControllerStatus(controller, "post-start");
		logPorts(controller);

		if (!submitNoopCommand(controller)) {
			printf("XHCI: Failed to submit No-Op command.");
			fflush(stdout);
			return;
		}

		printf("XHCI: Submitted No-Op command.");
		fflush(stdout);

		for (int i = 0; i < 100; ++i) {
			const uint32_t drained = drainEvents(controller, loggedEvents);

			if (drained != 0) {
				printf("XHCI: Polled %u event(s) after No-Op command.", drained);
				fflush(stdout);
				return;
			}

			usleep(1000);
		}

		logControllerStatus(controller, "after No-Op timeout");
		printf("XHCI: No event observed after No-Op command.");
		fflush(stdout);
	}

	auto contextSize(const MappedController &controller) -> uint32_t {
		return controller.uses64ByteContexts ? XHCI_CONTEXT_SIZE_64 : XHCI_CONTEXT_SIZE_32;
	}

	auto contextPtr(const AllocatedPage &page, const MappedController &controller, const uint32_t index) -> uint32_t * {
		return reinterpret_cast<uint32_t *>(page.virt + index * contextSize(controller));
	}

	void setContextDword(const AllocatedPage &page, const MappedController &controller, const uint32_t index, const uint32_t dword, const uint32_t value) {
		contextPtr(page, controller, index)[dword] = value;
	}

	auto portSpeed(const MappedController &controller, const uint8_t port) -> uint8_t {
		const uint32_t portsc = mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + (port - 1) * XHCI_OP_PORT_STRIDE + XHCI_PORTSC);
		return static_cast<uint8_t>((portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
	}

	auto ep0MaxPacketForSpeed(const uint8_t speed) -> uint16_t {
		if (speed >= 4) {
			return 512;
		}

		if (speed == 3) {
			return 64;
		}

		return 8;
	}

	auto waitForTransferEvent(MappedController &controller, const uint8_t slotId, const uint8_t endpointId, XhciTrb &completion, const int timeoutMs = 1000) -> bool {
		uint32_t ignoredLogs = 32;

		for (int i = 0; i < timeoutMs; ++i) {
			auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);

			for (;;) {
				auto &event = events[controller.memory.eventDequeueIndex];

				if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
					break;
				}

				const uint32_t type = eventType(event);
				const uint8_t eventSlot = static_cast<uint8_t>((event.control >> 24) & 0xFFU);
				const uint8_t eventEndpoint = static_cast<uint8_t>((event.control >> 16) & 0x1FU);
				const bool matched = type == XHCI_TRB_TYPE_TRANSFER_EVENT and eventSlot == slotId and eventEndpoint == endpointId;

				if (matched) {
					completion = event;
				} else if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
					const uint64_t eventParam = static_cast<uint64_t>(event.parameterLow) | (static_cast<uint64_t>(event.parameterHigh) << 32);
					const uint32_t portId = static_cast<uint32_t>(eventParam >> 24);
					printf("XHCI: Port status change event for port %u during transfer wait.", portId);
					fflush(stdout);
				} else if (ignoredLogs != 0) {
					printf("XHCI: Ignored event type=%u code=%u slot=%u ep=%u while waiting for transfer slot=%u ep=%u.",
					       type,
					       completionCode(event),
					       eventSlot,
					       eventEndpoint,
					       slotId,
					       endpointId);
					fflush(stdout);
					--ignoredLogs;
				}

				controller.memory.eventDequeueIndex++;

				if (controller.memory.eventDequeueIndex == XHCI_EVENT_RING_TRBS) {
					controller.memory.eventDequeueIndex = 0;
					controller.memory.eventConsumerCycle ^= 1;
				}

				updateEventDequeuePointer(controller);

				if (matched) {
					acknowledgeEvents(controller);
					return true;
				}
			}

			usleep(1000);
		}

		return false;
	}

	auto enqueueTransferTrb(XhciDevice &device, const XhciTrb &trb) -> bool {
		if (device.transferEnqueueIndex >= XHCI_TRANSFER_RING_TRBS - 1) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(device.transferRing.virt);
		auto &slot = ring[device.transferEnqueueIndex];
		slot = trb;

		if (device.transferProducerCycle != 0) {
			slot.control |= XHCI_TRB_CYCLE;
		} else {
			slot.control &= ~XHCI_TRB_CYCLE;
		}

		++device.transferEnqueueIndex;

		if (device.transferEnqueueIndex == XHCI_TRANSFER_RING_TRBS - 1) {
			device.transferEnqueueIndex = 0;
			device.transferProducerCycle ^= 1;
		}

		return true;
	}

	auto controlTransferIn(MappedController &controller,
	                       XhciDevice &device,
	                       const uint8_t requestType,
	                       const uint8_t request,
	                       const uint16_t value,
	                       const uint16_t index,
	                       const uint16_t length,
	                       const uint64_t dataPhys) -> bool {
		auto setup = XhciTrb();
		setup.parameterLow = static_cast<uint32_t>(requestType) | (static_cast<uint32_t>(request) << 8) | (static_cast<uint32_t>(value) << 16);
		setup.parameterHigh = static_cast<uint32_t>(index) | (static_cast<uint32_t>(length) << 16);
		setup.status = 8;
		setup.control = XHCI_TRB_IDT | (3U << 16) | (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT);

		auto data = XhciTrb();
		data.parameterLow = static_cast<uint32_t>(dataPhys);
		data.parameterHigh = static_cast<uint32_t>(dataPhys >> 32);
		data.status = length;
		data.control = XHCI_TRB_DIR_IN | XHCI_TRB_ISP | (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);

		auto status = XhciTrb();
		status.control = XHCI_TRB_IOC | (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT);

		if (!enqueueTransferTrb(device, setup) or !enqueueTransferTrb(device, data) or !enqueueTransferTrb(device, status)) {
			return false;
		}

		ringDoorbell(controller, device.slotId, 1);

		auto completion = XhciTrb();

		if (!waitForTransferEvent(controller, device.slotId, 1, completion, 1000)) {
			printf("XHCI: Control transfer timed out slot=%u.", device.slotId);
			fflush(stdout);
			return false;
		}

		const uint32_t code = completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS and code != XHCI_COMPLETION_SHORT_PACKET) {
			printf("XHCI: Control transfer failed slot=%u code=%u ctrl=0x%x status=0x%x.", device.slotId, code, completion.control, completion.status);
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto waitForPortReset(MappedController &controller, const uint8_t port) -> bool {
		const uint32_t offset = XHCI_OP_PORT_REGS + (port - 1) * XHCI_OP_PORT_STRIDE + XHCI_PORTSC;

		for (int i = 0; i < 250; ++i) {
			const uint32_t portsc = mmioRead32(controller.operationalBase, offset);

			if ((portsc & XHCI_PORTSC_PR) == 0 and (portsc & XHCI_PORTSC_PRC) != 0) {
				mmioWrite32(controller.operationalBase, offset, XHCI_PORTSC_PRC | XHCI_PORTSC_CSC | XHCI_PORTSC_PEC);
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto resetPortIfNeeded(MappedController &controller, const uint8_t port) -> bool {
		const uint32_t offset = XHCI_OP_PORT_REGS + (port - 1) * XHCI_OP_PORT_STRIDE + XHCI_PORTSC;
		uint32_t portsc = mmioRead32(controller.operationalBase, offset);

		if ((portsc & XHCI_PORTSC_CCS) == 0) {
			return false;
		}

		if ((portsc & XHCI_PORTSC_PED) != 0) {
			mmioWrite32(controller.operationalBase, offset, portsc & XHCI_PORTSC_CHANGE_BITS);
			return true;
		}

		mmioWrite32(controller.operationalBase, offset, (portsc & ~XHCI_PORTSC_CHANGE_BITS) | XHCI_PORTSC_PR);

		if (!waitForPortReset(controller, port)) {
			printf("XHCI: Port %u reset timed out.", port);
			fflush(stdout);
			return false;
		}

		portsc = mmioRead32(controller.operationalBase, offset);

		if ((portsc & XHCI_PORTSC_PED) == 0) {
			printf("XHCI: Port %u did not enable after reset PORTSC=0x%x.", port, portsc);
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto enableSlot(MappedController &controller, uint8_t &slotId) -> bool {
		auto command = XhciTrb();
		command.control = XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND << XHCI_TRB_TYPE_SHIFT;

		auto completion = XhciTrb();

		if (!runCommand(controller, command, completion)) {
			return false;
		}

		slotId = static_cast<uint8_t>((completion.control >> 24) & 0xFFU);

		if (slotId == 0) {
			printf("XHCI: Enable Slot completed with slot id 0.");
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto setupTransferRing(XhciDevice &device) -> bool {
		if (!allocatePage(device.transferRing)) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(device.transferRing.virt);
		auto &link = ring[XHCI_TRANSFER_RING_TRBS - 1];
		link.parameterLow = static_cast<uint32_t>(device.transferRing.phys);
		link.parameterHigh = static_cast<uint32_t>(device.transferRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		return true;
	}

	auto setupDeviceContexts(MappedController &controller, XhciDevice &device) -> bool {
		if (!allocatePage(device.inputContext) or !allocatePage(device.deviceContext) or !allocatePage(device.descriptorBuffer) or !setupTransferRing(device)) {
			return false;
		}

		auto *dcbaa = reinterpret_cast<uint64_t *>(controller.memory.dcbaa.virt);
		dcbaa[device.slotId] = device.deviceContext.phys;

		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, 0x3);

		const uint32_t routeString = 0;
		const uint32_t slotDword0 = routeString | (static_cast<uint32_t>(device.speed) << 20) | (1U << 27);
		const uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;

		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);

		const uint64_t dequeue = device.transferRing.phys | XHCI_TRB_CYCLE;
		const uint32_t ep0Dword1 = (4U << 3) | (3U << 1);
		const uint32_t ep0Dword4 = 8U | (static_cast<uint32_t>(device.maxPacketSize) << 16);

		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 1, ep0Dword1);
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 2, static_cast<uint32_t>(dequeue));
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 3, static_cast<uint32_t>(dequeue >> 32));
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 4, ep0Dword4);

		return true;
	}

	auto addressDevice(MappedController &controller, XhciDevice &device) -> bool {
		auto command = XhciTrb();
		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();
		return runCommand(controller, command, completion, 1000);
	}

	auto readDeviceDescriptor(MappedController &controller, XhciDevice &device, const uint16_t length) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller,
		                       device,
		                       0x80,
		                       USB_REQUEST_GET_DESCRIPTOR,
		                       static_cast<uint16_t>(USB_DESCRIPTOR_DEVICE << 8),
		                       0,
		                       length,
		                       device.descriptorBuffer.phys)) {
			return false;
		}

		auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);

		if (desc[0] < 8 or desc[1] != USB_DESCRIPTOR_DEVICE) {
			printf("XHCI: Invalid device descriptor header slot=%u len=%u type=%u.", device.slotId, desc[0], desc[1]);
			fflush(stdout);
			return false;
		}

		if (length >= 18) {
			const uint16_t vendor = static_cast<uint16_t>(desc[8]) | (static_cast<uint16_t>(desc[9]) << 8);
			const uint16_t product = static_cast<uint16_t>(desc[10]) | (static_cast<uint16_t>(desc[11]) << 8);
			printf("XHCI: Device slot=%u port=%u descriptor vid=%04x pid=%04x class=%02x subclass=%02x protocol=%02x maxPacket=%u.",
			       device.slotId,
			       device.rootPort,
			       vendor,
			       product,
			       desc[4],
			       desc[5],
			       desc[6],
			       desc[7]);
			fflush(stdout);
		} else {
			printf("XHCI: Device slot=%u port=%u descriptor header maxPacket=%u.", device.slotId, device.rootPort, desc[7]);
			fflush(stdout);
		}

		return true;
	}

	void enumerateRootPorts(MappedController &controller) {
		for (uint8_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portsc = mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + (port - 1) * XHCI_OP_PORT_STRIDE + XHCI_PORTSC);

			if ((portsc & XHCI_PORTSC_CCS) == 0) {
				continue;
			}

			if (!resetPortIfNeeded(controller, port)) {
				continue;
			}

			auto device = XhciDevice();
			device.rootPort = port;
			device.speed = portSpeed(controller, port);
			device.maxPacketSize = ep0MaxPacketForSpeed(device.speed);

			if (!enableSlot(controller, device.slotId)) {
				continue;
			}

			if (!setupDeviceContexts(controller, device)) {
				printf("XHCI: Failed to allocate contexts for port %u slot %u.", port, device.slotId);
				fflush(stdout);
				continue;
			}

			if (!addressDevice(controller, device)) {
				printf("XHCI: Failed to address device on port %u slot %u.", port, device.slotId);
				fflush(stdout);
				continue;
			}

			printf("XHCI: Addressed device on port %u as slot %u speed=%u.", port, device.slotId, device.speed);
			fflush(stdout);

			if (readDeviceDescriptor(controller, device, 8)) {
				auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
				device.maxPacketSize = desc[7] == 9 ? 512 : desc[7];
				readDeviceDescriptor(controller, device, 18);
			}

			controller.devices.push_back(device);
		}

		printf("XHCI: Enumerated %zu root device(s).", controller.devices.size());
		fflush(stdout);
	}

	auto eventIrqHandler(void *ctx) -> void * {
		auto *controller = static_cast<MappedController *>(ctx);
		uint32_t loggedEvents = 0;
		auto irq = IrqReceiveData();
		auto recv = hos_msg();
		recv.buffer = &irq;
		recv.length = sizeof(irq);

		auto filter = filter_options();
		filter.whiteListTypes = new uint64_t[1] { IRQ_RECEIVE_MSG_TYPE };
		filter.whiteListCount = 1;

		printf("XHCI: Event IRQ handler waiting on port %lu.", controller->memory.eventPort);
		fflush(stdout);

		for (;;) {
			irq = {};
			recv.buffer = &irq;
			recv.length = sizeof(irq);

			if (receive_horizonos_message(controller->memory.eventPort, &recv, &filter) != 0) {
				continue;
			}

			const uint32_t drained = drainEvents(*controller, loggedEvents);

			if (drained == 0 and loggedEvents < 32) {
				printf("XHCI: MSI-X vector %u irq=%lu cpu=%lu had no completed events.", controller->memory.msixVector, irq.irqNum, irq.cpuId);
				fflush(stdout);
				++loggedEvents;
			}
		}
	}

	auto startEventIrqHandler(MappedController &controller) -> bool {
		pthread_t thread {};
		int err = 0;

		for (int attempt = 1; attempt <= 20; ++attempt) {
			err = pthread_create(&thread, nullptr, eventIrqHandler, &controller);

			if (err == 0) {
				eventThreads.push_back(thread);

				return true;
			}

			usleep(10000);
		}

		if (err != 0) {
			printf("XHCI: Failed to start event IRQ handler for %02x:%02x.%x err=%d.", controller.pci.bus, controller.pci.device, controller.pci.function, err);
			fflush(stdout);
			return false;
		}

		return true;
	}

	auto setupMsix(MappedController &controller) -> bool {
		if (register_horizonos_port(reinterpret_cast<long *>(&controller.memory.eventPort)) != 0 or controller.memory.eventPort == 0) {
			return false;
		}

		msixGlobalEnable(controller.pci);

		controller.memory.msixVector = msixAllocVector(controller.pci, 0, controller.memory.eventPort);

		if (controller.memory.msixVector == 0) {
			return false;
		}

		printf("XHCI: MSI-X entry 0 vector=%u eventPort=%lu.", controller.memory.msixVector, controller.memory.eventPort);
		fflush(stdout);

		return true;
	}

	auto setupControllerMemory(MappedController &controller, const uint32_t maxScratchpads) -> bool {
		if (!allocatePage(controller.memory.dcbaa)) {
			return false;
		}

		if (!setupScratchpads(controller.memory, maxScratchpads)) {
			return false;
		}

		if (!setupCommandRing(controller.memory, controller.operationalBase)) {
			return false;
		}

		if (!setupEventRing(controller.memory, controller.runtimeBase)) {
			return false;
		}

		mmioWrite64(controller.operationalBase, XHCI_OP_DCBAAP, controller.memory.dcbaa.phys);

		return true;
	}

	auto bringUpController(MappedController &controller, const size_t index) -> bool {
		const uint64_t base = controller.mmioVirt;
		const uint8_t capLength = mmioRead8(base, XHCI_CAP_CAPLENGTH);
		const uint32_t firstCapDword = mmioRead32(base, 0x00);
		const uint16_t version = static_cast<uint16_t>(firstCapDword >> 16);
		const uint32_t hcsParams1 = mmioRead32(base, XHCI_CAP_HCSPARAMS1);
		const uint32_t hcsParams2 = mmioRead32(base, XHCI_CAP_HCSPARAMS2);
		const uint32_t hccParams1 = mmioRead32(base, XHCI_CAP_HCCPARAMS1);
		const uint32_t doorbellOffset = mmioRead32(base, XHCI_CAP_DBOFF) & ~0x3U;
		const uint32_t runtimeOffset = mmioRead32(base, XHCI_CAP_RTSOFF) & ~0x1FU;

		if (capLength == 0 or doorbellOffset == 0 or runtimeOffset == 0) {
			printf("XHCI: Controller %zu has invalid register layout cap=%u db=0x%x rt=0x%x.", index, capLength, doorbellOffset, runtimeOffset);
			fflush(stdout);
			return false;
		}

		const uint64_t operationalBase = base + capLength;
		const uint64_t runtimeBase = base + runtimeOffset;
		const uint64_t doorbellBase = base + doorbellOffset;
		const uint32_t maxSlots = hcsParams1 & 0xFFU;
		const uint32_t maxInterrupters = (hcsParams1 >> 8) & 0x7FFU;
		const uint32_t maxPorts = (hcsParams1 >> 24) & 0xFFU;
		const uint32_t maxScratchpads = maxScratchpadBuffers(hcsParams2);
		const uint32_t pageSizeMask = mmioRead32(operationalBase, XHCI_OP_PAGESIZE);

		controller.operationalBase = operationalBase;
		controller.runtimeBase = runtimeBase;
		controller.doorbellBase = doorbellBase;
		controller.maxSlots = maxSlots;
		controller.maxPorts = maxPorts;
		controller.maxInterrupters = maxInterrupters;
		controller.uses64ByteContexts = (hccParams1 & (1U << 2)) != 0;

		printf("XHCI: Controller %zu %02x:%02x.%x BAR=0x%lx size=0x%lx cap0=0x%x versionRaw=0x%x version=%x.%02x slots=%u ports=%u interrupters=%u scratchpads=%u pageMask=0x%x hcs2=0x%x hcc=0x%x.",
		       index,
		       controller.pci.bus,
		       controller.pci.device,
		       controller.pci.function,
		       controller.barPhys,
		       controller.barSize,
		       firstCapDword,
		       version,
		       version >> 8,
		       version & 0xFF,
		       maxSlots,
		       maxPorts,
		       maxInterrupters,
		       maxScratchpads,
		       pageSizeMask,
		       hcsParams2,
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

		if (!setupControllerMemory(controller, maxScratchpads)) {
			printf("XHCI: Controller %zu failed to allocate controller memory.", index);
			fflush(stdout);
			releaseControllerMemory(controller.memory);
			return false;
		}

		const uint32_t configSlots = min<uint32_t>(maxSlots, XHCI_MAX_CONFIGURED_SLOTS);
		controller.configuredSlots = configSlots;
		mmioWrite32(operationalBase, XHCI_OP_CONFIG, configSlots);

		if (!setupMsix(controller)) {
			printf("XHCI: Controller %zu failed to configure MSI-X.", index);
			fflush(stdout);
			releaseControllerMemory(controller.memory);
			return false;
		}

		printf("XHCI: Controller %zu prepared, configured %u device slot(s), DCBAA=0x%lx CR=0x%lx ER=0x%lx ERST=0x%lx.",
		       index,
		       configSlots,
		       controller.memory.dcbaa.phys,
		       controller.memory.commandRing.phys,
		       controller.memory.eventRing.phys,
		       controller.memory.erst.phys);
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

		auto &activeController = controllers.back();

		setInterrupterEnabled(activeController, false);

		if (!startController(activeController.operationalBase, false)) {
			printf("XHCI: Controller %zu failed to start.", controllers.size() - 1);
			fflush(stdout);
			continue;
		}

		printf("XHCI: Controller %zu started, configured %u device slot(s).", controllers.size() - 1, activeController.configuredSlots);
		fflush(stdout);

		postStartProbe(activeController);

		enumerateRootPorts(activeController);

		if (!startEventIrqHandler(activeController)) {
			continue;
		}

		setInterrupterEnabled(activeController, true);
		setControllerInterruptsEnabled(activeController.operationalBase, true);

		logControllerStatus(activeController, "irq-enabled");
	}

	printf("XHCI: %zu controller(s) initialized.", controllers.size());
	fflush(stdout);

	for (;;) {
		usleep(1000000);
	}
}
