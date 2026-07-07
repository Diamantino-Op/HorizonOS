#include "Xhci.hpp"
#include "MassStorage.hpp"

#include "horizonos/generic.h"
#include "pthread.h"
#include "sys/mman.h"
#include "unistd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {
	uint64_t xhciPort = 0;
	uint64_t pciPort = 0;
	uint64_t storagePort = 0;
	uint64_t storageReplyPort = 0;
	vector<pthread_t> eventThreads;
	UsbMassStorageDriver massStorageDriver;
	vector<MappedController> *activeControllers = nullptr;
	mutex eventRingMutex;
	mutex usbStorageMutex;

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
		const uint64_t barPhys = is64Bit ? (static_cast<uint64_t>(bar0Hi) << 32) | (bar0Lo & ~0xFULL) : (bar0Lo & ~0xFULL);

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

		const uint64_t sizeMask = is64Bit ? (static_cast<uint64_t>(sizeHi) << 32) | sizeLo : sizeLo;
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
		const uint64_t eventDequeuePhys = controller.memory.eventRing.phys + (controller.memory.eventDequeueIndex * sizeof(XhciTrb));

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
		return controller.memory.commandRing.phys + (index * sizeof(XhciTrb));
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
			const auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);

			for (;;) {
				const auto &event = events[controller.memory.eventDequeueIndex];

				if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
					break;
				}

				const uint32_t type = eventType(event);
				const uint64_t eventParam = static_cast<uint64_t>(event.parameterLow) | (static_cast<uint64_t>(event.parameterHigh) << 32);
				const bool matched = type == XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT and eventParam == commandTrbPhys;

				if (matched) {
					completion = event;
				} else if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
					const auto portId = static_cast<uint32_t>(eventParam >> 24);

					printf("XHCI: Port status change event for port %u during command wait.", portId);
					fflush(stdout);
				} else if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
					controller.pendingTransferEvents.push_back(event);
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
		unique_lock lock(eventRingMutex);
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
		unique_lock lock(eventRingMutex);
		uint32_t drained = 0;

		for (;;) {
			const auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);
			const auto &event = events[controller.memory.eventDequeueIndex];

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
		const uint32_t usbCmd = mmioRead32(controller.operationalBase, XHCI_OP_USBCMD);
		const uint32_t usbSts = mmioRead32(controller.operationalBase, XHCI_OP_USBSTS);
		const uint32_t iman = mmioRead32(controller.runtimeBase + 0x20, XHCI_INTERRUPTER_IMAN);

		printf("XHCI: %s status USBCMD=0x%x USBSTS=0x%x IMAN=0x%x.", phase, usbCmd, usbSts, iman);
		fflush(stdout);
	}

	void logPorts(const MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portOffset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
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
		const uint32_t portsc = mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC);

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

	auto usbEndpointId(const uint8_t endpointAddress) -> uint8_t {
		const uint8_t endpointNumber = endpointAddress & 0x0FU;
		const bool in = (endpointAddress & 0x80U) != 0;

		return static_cast<uint8_t>((endpointNumber * 2) + (in ? 1 : 0));
	}

	auto xhciEndpointType(const uint8_t endpointAddress, const uint8_t attributes) -> uint8_t {
		const bool in = (endpointAddress & 0x80U) != 0;

		switch (attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) {
			case USB_ENDPOINT_TRANSFER_ISOCHRONOUS:
				return in ? 5 : 1;
			case USB_ENDPOINT_TRANSFER_BULK:
				return in ? 6 : 2;
			case USB_ENDPOINT_TRANSFER_INTERRUPT:
				return in ? 7 : 3;
			default:
				return 0;
		}
	}

	auto contextIndexForEndpointId(const uint8_t endpointId) -> uint32_t {
		return static_cast<uint32_t>(endpointId) + 1;
	}

	auto waitForTransferEvent(MappedController &controller, const uint8_t slotId, const uint8_t endpointId, XhciTrb &completion, const int timeoutMs = 1000, const bool logTimeoutPath = true) -> bool {
		uint32_t ignoredLogs = 32;

		for (int i = 0; i < timeoutMs; ++i) {
			for (auto it = controller.pendingTransferEvents.begin(); it != controller.pendingTransferEvents.end(); ++it) {
				const auto eventSlot = static_cast<uint8_t>((it->control >> 24) & 0xFFU);
				const auto eventEndpoint = static_cast<uint8_t>((it->control >> 16) & 0x1FU);

				if (eventSlot == slotId and eventEndpoint == endpointId) {
					completion = *it;
					controller.pendingTransferEvents.erase(it);

					return true;
				}
			}

			const auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);

			for (;;) {
				const auto &event = events[controller.memory.eventDequeueIndex];

				if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
					break;
				}

				const uint32_t type = eventType(event);
				const auto eventSlot = static_cast<uint8_t>((event.control >> 24) & 0xFFU);
				const auto eventEndpoint = static_cast<uint8_t>((event.control >> 16) & 0x1FU);
				const bool matched = type == XHCI_TRB_TYPE_TRANSFER_EVENT and eventSlot == slotId and eventEndpoint == endpointId;

				if (matched) {
					completion = event;
				} else if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
					const uint64_t eventParam = static_cast<uint64_t>(event.parameterLow) | (static_cast<uint64_t>(event.parameterHigh) << 32);
					const auto portId = static_cast<uint32_t>(eventParam >> 24);

					if (logTimeoutPath) {
						printf("XHCI: Port status change event for port %u during transfer wait.", portId);
						fflush(stdout);
					}
				} else if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
					controller.pendingTransferEvents.push_back(event);
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

	auto enqueueEndpointTrb(UsbEndpoint &endpoint, const XhciTrb &trb) -> bool {
		if (endpoint.transferEnqueueIndex >= XHCI_TRANSFER_RING_TRBS - 1) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(endpoint.transferRing.virt);
		auto &slot = ring[endpoint.transferEnqueueIndex];
		slot = trb;

		if (endpoint.transferProducerCycle != 0) {
			slot.control |= XHCI_TRB_CYCLE;
		} else {
			slot.control &= ~XHCI_TRB_CYCLE;
		}

		++endpoint.transferEnqueueIndex;

		if (endpoint.transferEnqueueIndex == XHCI_TRANSFER_RING_TRBS - 1) {
			endpoint.transferEnqueueIndex = 0;
			endpoint.transferProducerCycle ^= 1;
		}

		return true;
	}

	void recoverEndpoint(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint);

	auto bulkOrInterruptTransfer(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, const uint32_t pageCount, const uint32_t length, const bool in, uint32_t *actualLength, const int timeoutMs = 5000, const bool logTimeout = true) -> bool {
		if (actualLength != nullptr) {
			*actualLength = 0;
		}

		if (endpoint.transferRing.phys == 0 or pagePhysArray == nullptr or pageCount == 0 or length == 0) {
			return false;
		}

		unique_lock lock(eventRingMutex);
		uint32_t remaining = length;

		for (uint32_t page = 0; page < pageCount and remaining != 0; ++page) {
			const uint32_t chunk = min<uint32_t>(remaining, XHCI_PAGE_SIZE);
			auto trb = XhciTrb();

			trb.parameterLow = static_cast<uint32_t>(pagePhysArray[page]);
			trb.parameterHigh = static_cast<uint32_t>(pagePhysArray[page] >> 32);
			trb.status = chunk;
			trb.control = XHCI_TRB_ISP | (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT);

			if (in) {
				trb.control |= XHCI_TRB_DIR_IN;
			}

			remaining -= chunk;

			if (remaining == 0) {
				trb.control |= XHCI_TRB_IOC;
			} else {
				trb.control |= XHCI_TRB_CHAIN;
			}

			if (!enqueueEndpointTrb(endpoint, trb)) {
				return false;
			}
		}

		if (remaining != 0) {
			return false;
		}

		ringDoorbell(controller, device.slotId, endpoint.endpointId);

		auto completion = XhciTrb();

		if (!waitForTransferEvent(controller, device.slotId, endpoint.endpointId, completion, timeoutMs, logTimeout)) {
			if (logTimeout) {
				printf("XHCI: Endpoint transfer timed out slot=%u ep=0x%02x.", device.slotId, endpoint.address);
				fflush(stdout);
			}

			lock.unlock();
			recoverEndpoint(controller, device, endpoint);

			return false;
		}

		const uint32_t code = completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS and code != XHCI_COMPLETION_SHORT_PACKET) {
			printf("XHCI: Endpoint transfer failed slot=%u ep=0x%02x code=%u status=0x%x.", device.slotId, endpoint.address, code, completion.status);
			fflush(stdout);

			return false;
		}

		const uint32_t residue = completion.status & 0xFFFFFFU;

		if (actualLength != nullptr) {
			*actualLength = length >= residue ? length - residue : 0;
		}

		return true;
	}

	auto controlTransferIn(MappedController &controller, XhciDevice &device, const uint8_t requestType, const uint8_t request, const uint16_t value, const uint16_t index, const uint16_t length, const uint64_t dataPhys) -> bool {
		const scoped_lock lock(eventRingMutex);
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

	auto controlTransferNoData(MappedController &controller, XhciDevice &device, const uint8_t requestType, const uint8_t request, const uint16_t value, const uint16_t index) -> bool {
		const scoped_lock lock(eventRingMutex);
		auto setup = XhciTrb();

		setup.parameterLow = static_cast<uint32_t>(requestType) | (static_cast<uint32_t>(request) << 8) | (static_cast<uint32_t>(value) << 16);
		setup.parameterHigh = static_cast<uint32_t>(index);
		setup.status = 8;
		setup.control = XHCI_TRB_IDT | (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT);

		auto status = XhciTrb();

		status.control = XHCI_TRB_DIR_IN | XHCI_TRB_IOC | (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT);

		if (!enqueueTransferTrb(device, setup) or !enqueueTransferTrb(device, status)) {
			return false;
		}

		ringDoorbell(controller, device.slotId, 1);

		auto completion = XhciTrb();

		if (!waitForTransferEvent(controller, device.slotId, 1, completion, 1000)) {
			printf("XHCI: Control no-data transfer timed out slot=%u request=%u.", device.slotId, request);
			fflush(stdout);

			return false;
		}

		const uint32_t code = completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS) {
			printf("XHCI: Control no-data transfer failed slot=%u request=%u code=%u ctrl=0x%x status=0x%x.", device.slotId, request, code, completion.control, completion.status);
			fflush(stdout);

			return false;
		}

		return true;
	}

	auto waitForPortReset(const MappedController &controller, const uint8_t port) -> bool {
		const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;

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

	auto resetPortIfNeeded(const MappedController &controller, const uint8_t port) -> bool {
		const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
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

	auto setupEndpointTransferRing(UsbEndpoint &endpoint) -> bool {
		if (!allocatePage(endpoint.transferRing)) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(endpoint.transferRing.virt);
		auto &link = ring[XHCI_TRANSFER_RING_TRBS - 1];

		link.parameterLow = static_cast<uint32_t>(endpoint.transferRing.phys);
		link.parameterHigh = static_cast<uint32_t>(endpoint.transferRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		endpoint.transferEnqueueIndex = 0;
		endpoint.transferProducerCycle = 1;

		return true;
	}

	void resetEndpointTransferRing(UsbEndpoint &endpoint) {
		if (endpoint.transferRing.virt == 0) {
			return;
		}

		memset(reinterpret_cast<void *>(endpoint.transferRing.virt), 0, XHCI_PAGE_SIZE);

		auto *ring = reinterpret_cast<XhciTrb *>(endpoint.transferRing.virt);
		auto &link = ring[XHCI_TRANSFER_RING_TRBS - 1];

		link.parameterLow = static_cast<uint32_t>(endpoint.transferRing.phys);
		link.parameterHigh = static_cast<uint32_t>(endpoint.transferRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		endpoint.transferEnqueueIndex = 0;
		endpoint.transferProducerCycle = 1;
	}

	auto setupDeviceContexts(const MappedController &controller, XhciDevice &device) -> bool {
		if (!allocatePage(device.inputContext) or !allocatePage(device.deviceContext) or !allocatePage(device.descriptorBuffer) or !setupTransferRing(device)) {
			return false;
		}

		auto *dcbaa = reinterpret_cast<uint64_t *>(controller.memory.dcbaa.virt);

		dcbaa[device.slotId] = device.deviceContext.phys;

		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, 0x3);

		const uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (1U << 27);
		const uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;

		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);

		const uint64_t dequeue = device.transferRing.phys | XHCI_TRB_CYCLE;
		constexpr uint32_t ep0Dword1 = (4U << 3) | (3U << 1);
		const uint32_t ep0Dword4 = 8U | (static_cast<uint32_t>(device.maxPacketSize) << 16);

		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 1, ep0Dword1);
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 2, static_cast<uint32_t>(dequeue));
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 3, static_cast<uint32_t>(dequeue >> 32));
		setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 4, ep0Dword4);

		return true;
	}

	auto addressDevice(MappedController &controller, const XhciDevice &device) -> bool {
		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	auto disableSlot(MappedController &controller, const XhciDevice &device) -> bool {
		if (device.slotId == 0) {
			return true;
		}

		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	auto stopEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_STOP_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	auto resetEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_RESET_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	auto setEndpointDequeuePointer(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();
		const uint64_t dequeue = endpoint.transferRing.phys | XHCI_TRB_CYCLE;

		command.parameterLow = static_cast<uint32_t>(dequeue);
		command.parameterHigh = static_cast<uint32_t>(dequeue >> 32);
		command.control = (XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	void recoverEndpoint(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint) {
		stopEndpoint(controller, device, endpoint);
		resetEndpoint(controller, device, endpoint);
		resetEndpointTransferRing(endpoint);
		setEndpointDequeuePointer(controller, device, endpoint);
	}

	void releaseDeviceMemory(const MappedController &controller, XhciDevice &device) {
		if (device.slotId != 0 and controller.memory.dcbaa.virt != 0) {
			auto *dcbaa = reinterpret_cast<uint64_t *>(controller.memory.dcbaa.virt);

			dcbaa[device.slotId] = 0;
		}

		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
				freePage(endpoint.transferRing);
			}
		}

		device.interfaces.clear();

		freePage(device.descriptorBuffer);
		freePage(device.hubInterruptBuffer);
		freePage(device.transferRing);
		freePage(device.deviceContext);
		freePage(device.inputContext);

		device = {};
	}

	auto evaluateEp0Context(MappedController &controller, const XhciDevice &device) -> bool {
		if (device.inputContext.virt == 0) {
			return false;
		}

		memset(reinterpret_cast<void *>(device.inputContext.virt), 0, XHCI_PAGE_SIZE);

		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, 0x3);

		const uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (1U << 27);
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

		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_EVALUATE_CONTEXT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return runCommand(controller, command, completion, 1000);
	}

	auto configureEndpoints(MappedController &controller, XhciDevice &device) -> bool {
		if (device.inputContext.virt == 0) {
			return false;
		}

		memset(reinterpret_cast<void *>(device.inputContext.virt), 0, XHCI_PAGE_SIZE);

		uint32_t addFlags = 0x1;
		uint8_t maxEndpointId = 1;

		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
				if (endpoint.transferRing.phys == 0 and !setupEndpointTransferRing(endpoint)) {
					printf("XHCI: Failed to allocate transfer ring slot=%u ep=0x%02x.", device.slotId, endpoint.address);
					fflush(stdout);

					return false;
				}

				addFlags |= 1U << endpoint.endpointId;
				maxEndpointId = max<uint8_t>(maxEndpointId, endpoint.endpointId);
			}
		}

		if (maxEndpointId == 1) {
			return true;
		}

		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, addFlags);

		uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (static_cast<uint32_t>(maxEndpointId) << 27);
		uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;

		if (device.isHub) {
			slotDword0 |= 1U << 26;
			slotDword1 |= static_cast<uint32_t>(device.hubPortCount) << 24;
		}

		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
		setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);

		for (auto &interface : device.interfaces) {
			for (const auto &endpoint : interface.endpoints) {
				const uint64_t dequeue = endpoint.transferRing.phys | XHCI_TRB_CYCLE;
				const uint32_t ctxIndex = contextIndexForEndpointId(endpoint.endpointId);
				const uint32_t interval = static_cast<uint32_t>(endpoint.interval) << 16;
				const uint32_t epDword1 = (static_cast<uint32_t>(endpoint.endpointType) << 3) | (3U << 1);
				const uint32_t avgTrbLength = min<uint32_t>(endpoint.maxPacketSize, 0xFFFF);
				const uint32_t epDword4 = avgTrbLength | (static_cast<uint32_t>(endpoint.maxPacketSize) << 16);

				setContextDword(device.inputContext, controller, ctxIndex, 0, interval);
				setContextDword(device.inputContext, controller, ctxIndex, 1, epDword1);
				setContextDword(device.inputContext, controller, ctxIndex, 2, static_cast<uint32_t>(dequeue));
				setContextDword(device.inputContext, controller, ctxIndex, 3, static_cast<uint32_t>(dequeue >> 32));
				setContextDword(device.inputContext, controller, ctxIndex, 4, epDword4);
			}
		}

		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		if (!runCommand(controller, command, completion, 1000)) {
			printf("XHCI: Configure Endpoint failed slot=%u addFlags=0x%x.", device.slotId, addFlags);
			fflush(stdout);

			return false;
		}

		printf("XHCI: Configured endpoints slot=%u addFlags=0x%x.", device.slotId, addFlags);
		fflush(stdout);

		return true;
	}

	auto readDeviceDescriptor(MappedController &controller, XhciDevice &device, const uint16_t length) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_DEVICE << 8, 0, length, device.descriptorBuffer.phys)) {
			return false;
		}

		const auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);

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

	auto readStringDescriptor(MappedController &controller, XhciDevice &device, const uint8_t index) -> string {
		if (index == 0) {
			return {};
		}

		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, (USB_DESCRIPTOR_STRING << 8) | index, 0x0409, 255, device.descriptorBuffer.phys)) {
			return {};
		}

		const auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);

		if (desc[0] < 2 or desc[1] != USB_DESCRIPTOR_STRING) {
			return {};
		}

		string text;
		const uint32_t length = min<uint32_t>(desc[0], 255);

		for (uint32_t offset = 2; offset + 1 < length; offset += 2) {
			const uint16_t ch = static_cast<uint16_t>(desc[offset]) | (static_cast<uint16_t>(desc[offset + 1]) << 8);

			text.push_back(ch >= 0x20 and ch <= 0x7E ? static_cast<char>(ch) : '?');
		}

		return text;
	}

	void parseConfigurationDescriptor(XhciDevice &device, const uint8_t *desc, const uint16_t totalLength) {
		uint16_t offset = 0;
		UsbInterface *currentInterface = nullptr;

		device.interfaces.clear();

		while (offset + 2 <= totalLength) {
			const uint8_t length = desc[offset];
			const uint8_t type = desc[offset + 1];

			if (length < 2 or offset + length > totalLength) {
				printf("XHCI: Malformed descriptor slot=%u offset=%u len=%u type=%u total=%u.", device.slotId, offset, length, type, totalLength);
				fflush(stdout);

				return;
			}

			if (type == USB_DESCRIPTOR_INTERFACE and length >= 9) {
				const uint8_t interfaceNumber = desc[offset + 2];
				const uint8_t alternateSetting = desc[offset + 3];
				const uint8_t endpointCount = desc[offset + 4];
				const uint8_t interfaceClass = desc[offset + 5];
				const uint8_t interfaceSubclass = desc[offset + 6];
				const uint8_t interfaceProtocol = desc[offset + 7];
				auto interface = UsbInterface();

				interface.number = interfaceNumber;
				interface.alternateSetting = alternateSetting;
				interface.interfaceClass = interfaceClass;
				interface.interfaceSubclass = interfaceSubclass;
				interface.interfaceProtocol = interfaceProtocol;
				interface.endpoints.reserve(endpointCount);
				device.interfaces.push_back(interface);
				currentInterface = &device.interfaces.back();

				printf("XHCI: Interface slot=%u if=%u alt=%u endpoints=%u class=%02x subclass=%02x protocol=%02x.",
				       device.slotId,
				       interfaceNumber,
				       alternateSetting,
				       endpointCount,
				       interfaceClass,
				       interfaceSubclass,
				       interfaceProtocol);
				fflush(stdout);

			} else if (type == USB_DESCRIPTOR_ENDPOINT and length >= 7) {
				const uint8_t endpointAddress = desc[offset + 2];
				const uint8_t attributes = desc[offset + 3];
				const uint16_t maxPacket = static_cast<uint16_t>(desc[offset + 4]) | (static_cast<uint16_t>(desc[offset + 5]) << 8);
				const uint8_t interval = desc[offset + 6];
				auto endpoint = UsbEndpoint();

				endpoint.address = endpointAddress;
				endpoint.attributes = attributes;
				endpoint.maxPacketSize = maxPacket;
				endpoint.interval = interval;
				endpoint.endpointId = usbEndpointId(endpointAddress);
				endpoint.endpointType = xhciEndpointType(endpointAddress, attributes);

				if (currentInterface != nullptr and endpoint.endpointId != 0 and endpoint.endpointType != 0) {
					currentInterface->endpoints.push_back(endpoint);
				}

				printf("XHCI: Endpoint slot=%u addr=0x%02x attrs=0x%02x maxPacket=%u interval=%u.",
				       device.slotId,
				       endpointAddress,
				       attributes,
				       maxPacket,
				       interval);
				fflush(stdout);
			}

			offset += length;
		}
	}

	auto readConfigurationDescriptor(MappedController &controller, XhciDevice &device, uint8_t &configurationValue) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_CONFIGURATION << 8, 0, 9, device.descriptorBuffer.phys)) {
			return false;
		}

		const auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);

		if (desc[0] < 9 or desc[1] != USB_DESCRIPTOR_CONFIGURATION) {
			printf("XHCI: Invalid configuration descriptor header slot=%u len=%u type=%u.", device.slotId, desc[0], desc[1]);
			fflush(stdout);

			return false;
		}

		uint16_t totalLength = static_cast<uint16_t>(desc[2]) | (static_cast<uint16_t>(desc[3]) << 8);

		totalLength = std::max<uint16_t>(totalLength, 9);

		if (totalLength > XHCI_PAGE_SIZE) {
			printf("XHCI: Configuration descriptor too large slot=%u total=%u, truncating to %u.", device.slotId, totalLength, XHCI_PAGE_SIZE);
			fflush(stdout);

			totalLength = XHCI_PAGE_SIZE;
		}

		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_CONFIGURATION << 8, 0, totalLength, device.descriptorBuffer.phys)) {
			return false;
		}

		desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
		configurationValue = desc[5];

		printf("XHCI: Configuration slot=%u value=%u interfaces=%u attributes=0x%02x maxPower=%u total=%u.",
		       device.slotId,
		       configurationValue,
		       desc[4],
		       desc[7],
		       desc[8],
		       totalLength);
		fflush(stdout);

		parseConfigurationDescriptor(device, desc, totalLength);

		return true;
	}

	auto setConfiguration(MappedController &controller, XhciDevice &device, const uint8_t configurationValue) -> bool {
		if (!controlTransferNoData(controller, device, 0x00, USB_REQUEST_SET_CONFIGURATION, configurationValue, 0)) {
			return false;
		}

		printf("XHCI: Set configuration slot=%u value=%u.", device.slotId, configurationValue);
		fflush(stdout);

		return true;
	}

	void bindClassDrivers(MappedController &controller, XhciDevice &device, uint32_t controllerId);
	void prepareHubMetadata(MappedController &controller, XhciDevice &device);
	void submitHubInterruptTransfer(MappedController &controller, XhciDevice &hub);

	auto enumerateDevice(MappedController &controller, const uint32_t controllerId, const uint8_t rootPort, const uint32_t routeString, const uint8_t depth, const uint8_t speed, const uint8_t parentSlotId, const uint8_t hubPort, const char *location) -> bool {
		if (controller.devices.size() >= controller.configuredSlots) {
			printf("XHCI: No free device slots for %s.", location);
			fflush(stdout);

			return false;
		}

		controller.devices.emplace_back();

		auto &device = controller.devices.back();

		device.controllerId = controllerId;
		device.rootPort = rootPort;
		device.parentSlotId = parentSlotId;
		device.hubPort = hubPort;
		device.routeString = routeString;
		device.depth = depth;
		device.speed = speed;
		device.maxPacketSize = ep0MaxPacketForSpeed(device.speed);

		if (!enableSlot(controller, device.slotId)) {
			controller.devices.pop_back();

			return false;
		}

		if (!setupDeviceContexts(controller, device)) {
			printf("XHCI: Failed to allocate contexts for %s slot %u.", location, device.slotId);
			fflush(stdout);

			disableSlot(controller, device);
			releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		if (!addressDevice(controller, device)) {
			printf("XHCI: Failed to address device on %s slot %u.", location, device.slotId);
			fflush(stdout);

			disableSlot(controller, device);
			releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		printf("XHCI: Addressed device on %s as slot %u speed=%u route=0x%x.", location, device.slotId, device.speed, device.routeString);
		fflush(stdout);

		if (readDeviceDescriptor(controller, device, 8)) {
			const auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
			const uint16_t actualMaxPacketSize = desc[7] == 9 ? 512 : desc[7];

			if (actualMaxPacketSize != device.maxPacketSize) {
				device.maxPacketSize = actualMaxPacketSize;

				if (!evaluateEp0Context(controller, device)) {
					printf("XHCI: Failed to evaluate EP0 context slot=%u maxPacket=%u.", device.slotId, device.maxPacketSize);
					fflush(stdout);

					disableSlot(controller, device);
					releaseDeviceMemory(controller, device);

					controller.devices.pop_back();

					return false;
				}
			}

			if (readDeviceDescriptor(controller, device, 18)) {
				const auto *fullDesc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
				const uint8_t manufacturerIndex = fullDesc[14];
				const uint8_t productIndex = fullDesc[15];
				const uint8_t serialIndex = fullDesc[16];

				device.manufacturer = readStringDescriptor(controller, device, manufacturerIndex);
				device.product = readStringDescriptor(controller, device, productIndex);
				device.serial = readStringDescriptor(controller, device, serialIndex);

				if (!device.manufacturer.empty() or !device.product.empty() or !device.serial.empty()) {
					printf("XHCI: Device slot=%u strings manufacturer='%s' product='%s' serial='%s'.", device.slotId, device.manufacturer.c_str(), device.product.c_str(), device.serial.c_str());
					fflush(stdout);
				}
			}
		}

		uint8_t configurationValue = 0;

		if (readConfigurationDescriptor(controller, device, configurationValue) and configurationValue != 0) {
			prepareHubMetadata(controller, device);

			if (!configureEndpoints(controller, device)) {
				printf("XHCI: Failed to configure endpoints slot=%u.", device.slotId);
				fflush(stdout);
			} else if (!setConfiguration(controller, device, configurationValue)) {
				printf("XHCI: Failed to set configuration slot=%u value=%u.", device.slotId, configurationValue);
				fflush(stdout);
			} else {
				device.configurationValue = configurationValue;
				device.configured = true;

				bindClassDrivers(controller, device, controllerId);
			}
		}

		return true;
	}

	auto hubPortStatus(MappedController &controller, XhciDevice &hub, const uint8_t port, uint16_t &status, uint16_t &change) -> bool {
		memset(reinterpret_cast<void *>(hub.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, hub, 0xA3, USB_REQUEST_GET_STATUS, 0, port, 4, hub.descriptorBuffer.phys)) {
			return false;
		}

		const auto *bytes = reinterpret_cast<uint8_t *>(hub.descriptorBuffer.virt);

		status = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
		change = static_cast<uint16_t>(bytes[2]) | (static_cast<uint16_t>(bytes[3]) << 8);

		return true;
	}

	auto hubPortSpeed(const uint16_t status) -> uint8_t {
		if ((status & USB_HUB_PORT_STATUS_HIGH_SPEED) != 0) {
			return 3;
		}

		if ((status & USB_HUB_PORT_STATUS_LOW_SPEED) != 0) {
			return 2;
		}

		return 1;
	}

	void clearHubPortChangeBits(MappedController &controller, XhciDevice &hub, const uint8_t port, const uint16_t change) {
		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_CONNECTION - 16))) != 0) {
			controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_CONNECTION, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_ENABLE - 16))) != 0) {
			controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_ENABLE, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_OVER_CURRENT - 16))) != 0) {
			controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_OVER_CURRENT, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_RESET - 16))) != 0) {
			controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_RESET, port);
		}
	}

	auto resetHubPort(MappedController &controller, XhciDevice &hub, const uint8_t port, uint16_t &status) -> bool {
		uint16_t oldStatus = 0;
		uint16_t oldChange = 0;

		if (hubPortStatus(controller, hub, port, oldStatus, oldChange)) {
			clearHubPortChangeBits(controller, hub, port, oldChange);
		}

		controlTransferNoData(controller, hub, 0x23, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_RESET, port);

		uint16_t change = 0;

		for (uint32_t attempt = 0; attempt < 100; ++attempt) {
			usleep(10000);

			if (!hubPortStatus(controller, hub, port, status, change)) {
				continue;
			}

			if ((change & (1U << (USB_HUB_FEATURE_C_PORT_RESET - 16))) != 0) {
				clearHubPortChangeBits(controller, hub, port, change);

				break;
			}
		}

		return (status & USB_HUB_PORT_STATUS_CONNECTION) != 0 and (status & USB_HUB_PORT_STATUS_ENABLE) != 0;
	}

	auto readHubDescriptor(MappedController &controller, XhciDevice &device, const UsbInterface &interface) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!controlTransferIn(controller, device, 0xA0, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_HUB << 8, 0, 9, device.descriptorBuffer.phys)) {
			printf("XHCI: Hub descriptor read failed slot=%u if=%u.", device.slotId, interface.number);
			fflush(stdout);

			return false;
		}

		auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
		const uint8_t portCount = desc[2];
		const uint16_t characteristics = static_cast<uint16_t>(desc[3]) | (static_cast<uint16_t>(desc[4]) << 8);
		const uint8_t powerOnDelayMs = static_cast<uint8_t>(min<uint32_t>(255, max<uint32_t>(20, static_cast<uint32_t>(desc[5]) * 2)));

		device.isHub = true;
		device.hubPortCount = portCount;
		device.hubCharacteristics = characteristics;
		device.hubPowerOnDelayMs = powerOnDelayMs;
		device.hubInterruptEndpointId = 0;
		device.hubInterruptEndpointAddress = 0;

		for (const auto &endpoint : interface.endpoints) {
			if ((endpoint.address & 0x80U) != 0 and (endpoint.attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_INTERRUPT) {
				device.hubInterruptEndpointId = endpoint.endpointId;
				device.hubInterruptEndpointAddress = endpoint.address;
				break;
			}
		}

		printf("XHCI: Hub slot=%u ports=%u characteristics=0x%04x powerDelayMs=%u irqEp=0x%02x.", device.slotId, portCount, characteristics, powerOnDelayMs, device.hubInterruptEndpointAddress);
		fflush(stdout);

		return portCount != 0;
	}

	void prepareHubMetadata(MappedController &controller, XhciDevice &device) {
		for (const auto &interface : device.interfaces) {
			if (interface.interfaceClass == USB_CLASS_HUB) {
				readHubDescriptor(controller, device, interface);
				return;
			}
		}
	}

	void probeHub(MappedController &controller, XhciDevice &device, const UsbInterface &interface) {
		if (!device.isHub and !readHubDescriptor(controller, device, interface)) {
			return;
		}

		for (uint8_t port = 1; port <= device.hubPortCount; ++port) {
			controlTransferNoData(controller, device, 0x23, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_POWER, port);
		}

		usleep(static_cast<useconds_t>(device.hubPowerOnDelayMs) * 1000);

		for (uint8_t port = 1; port <= device.hubPortCount; ++port) {
			uint16_t status = 0;
			uint16_t change = 0;

			if (!hubPortStatus(controller, device, port, status, change)) {
				continue;
			}

			clearHubPortChangeBits(controller, device, port, change);

			printf("XHCI: Hub slot=%u port=%u status=0x%04x change=0x%04x.", device.slotId, port, status, change);
			fflush(stdout);

			if ((status & USB_HUB_PORT_STATUS_CONNECTION) == 0) {
				continue;
			}

			if (device.depth >= 5 or port > 15) {
				printf("XHCI: Hub slot=%u port=%u cannot encode route depth=%u.", device.slotId, port, device.depth);
				fflush(stdout);

				continue;
			}

			if (!resetHubPort(controller, device, port, status)) {
				printf("XHCI: Hub slot=%u port=%u reset failed status=0x%04x.", device.slotId, port, status);
				fflush(stdout);

				continue;
			}

			const uint32_t childRouteString = device.routeString | (static_cast<uint32_t>(port) << (device.depth * 4));
			const uint8_t childSpeed = hubPortSpeed(status);
			char location[32] {};

			snprintf(location, sizeof(location), "hub%u-port%u", device.slotId, port);
			enumerateDevice(controller, device.controllerId, device.rootPort, childRouteString, static_cast<uint8_t>(device.depth + 1), childSpeed, device.slotId, port, location);
		}

		submitHubInterruptTransfer(controller, device);
	}

	void configureBootHid(MappedController &controller, XhciDevice &device, const UsbInterface &interface) {
		if (interface.interfaceSubclass != 1) {
			return;
		}

		const char *kind = interface.interfaceProtocol == 1 ? "keyboard" : (interface.interfaceProtocol == 2 ? "mouse" : "unknown");
		const bool protocolOk = controlTransferNoData(controller, device, 0x21, USB_HID_REQUEST_SET_PROTOCOL, 0, interface.number);
		const bool idleOk = controlTransferNoData(controller, device, 0x21, USB_HID_REQUEST_SET_IDLE, 0, interface.number);

		printf("XHCI/HID: Boot HID %s slot=%u if=%u endpointCount=%zu protocol=%d idle=%d input handling not installed.",
		       kind,
		       device.slotId,
		       interface.number,
		       interface.endpoints.size(),
		       protocolOk,
		       idleOk);
		fflush(stdout);
	}

	void bindClassDrivers(MappedController &controller, XhciDevice &device, const uint32_t controllerId) {
		for (auto &interface : device.interfaces) {
			if (interface.interfaceClass == USB_CLASS_HID and interface.interfaceSubclass == 1) {
				configureBootHid(controller, device, interface);
			} else if (interface.interfaceClass == USB_CLASS_HUB) {
				probeHub(controller, device, interface);
			} else if (interface.interfaceClass == USB_CLASS_MASS_STORAGE and interface.interfaceSubclass == USB_SUBCLASS_SCSI and interface.interfaceProtocol == USB_PROTOCOL_BULK_ONLY) {
				massStorageDriver.bind(controllerId, device, interface);
			}
		}
	}

	void enumerateRootPorts(MappedController &controller, const uint32_t controllerId) {
		controller.devices.reserve(max<uint32_t>(controller.maxPorts, controller.configuredSlots));

		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portsc = mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC);

			if ((portsc & XHCI_PORTSC_CCS) == 0) {
				continue;
			}

			if (!resetPortIfNeeded(controller, port)) {
				continue;
			}

			char location[16] {};

			snprintf(location, sizeof(location), "port%u", port);
			enumerateDevice(controller, controllerId, port, 0, 0, portSpeed(controller, port), 0, 0, location);
		}

		printf("XHCI: Enumerated %zu root device(s).", controller.devices.size());
		fflush(stdout);
	}

	void removeDevicesForRootPort(MappedController &controller, const uint8_t rootPort) {
		const scoped_lock storageLock(usbStorageMutex);

		for (auto it = controller.devices.begin(); it != controller.devices.end();) {
			if (it->rootPort != rootPort) {
				++it;
				continue;
			}

			printf("XHCI: Removing device slot=%u rootPort=%u route=0x%x.", it->slotId, it->rootPort, it->routeString);
			fflush(stdout);

			massStorageDriver.removeDevice(*it);
			disableSlot(controller, *it);
			releaseDeviceMemory(controller, *it);
			it = controller.devices.erase(it);
		}
	}

	void removeDevicesForHubPort(MappedController &controller, const uint8_t parentSlotId, const uint8_t hubPort) {
		const scoped_lock storageLock(usbStorageMutex);
		vector<uint8_t> removedSlots;
		bool progress = true;

		while (progress) {
			progress = false;

			for (auto it = controller.devices.begin(); it != controller.devices.end();) {
				const bool directChild = it->parentSlotId == parentSlotId and it->hubPort == hubPort;
				const bool descendant = ranges::find(removedSlots, it->parentSlotId) != removedSlots.end();

				if (!directChild and !descendant) {
					++it;
					continue;
				}

				printf("XHCI: Removing device slot=%u parent=%u hubPort=%u route=0x%x.", it->slotId, it->parentSlotId, it->hubPort, it->routeString);
				fflush(stdout);

				massStorageDriver.removeDevice(*it);
				removedSlots.push_back(it->slotId);
				disableSlot(controller, *it);
				releaseDeviceMemory(controller, *it);
				it = controller.devices.erase(it);
				progress = true;
			}
		}
	}

	auto findDeviceBySlot(MappedController &controller, const uint8_t slotId) -> XhciDevice * {
		for (auto &device : controller.devices) {
			if (device.slotId == slotId) {
				return &device;
			}
		}

		return nullptr;
	}

	auto hubHasChildOnPort(const MappedController &controller, const uint8_t parentSlotId, const uint8_t hubPort) -> bool {
		return ranges::any_of(controller.devices, [&](const XhciDevice &device) {
			return device.parentSlotId == parentSlotId and device.hubPort == hubPort;
		});
	}

	void handleHubPortChange(MappedController &controller, const uint8_t hubSlotId, const uint8_t port) {
		auto *hub = findDeviceBySlot(controller, hubSlotId);

		if (hub == nullptr or port == 0 or port > hub->hubPortCount) {
			return;
		}

		uint16_t status = 0;
		uint16_t change = 0;

		if (!hubPortStatus(controller, *hub, port, status, change)) {
			return;
		}

		clearHubPortChangeBits(controller, *hub, port, change);

		if ((change & ((1U << (USB_HUB_FEATURE_C_PORT_CONNECTION - 16)) | (1U << (USB_HUB_FEATURE_C_PORT_ENABLE - 16)))) == 0) {
			return;
		}

		if ((status & USB_HUB_PORT_STATUS_CONNECTION) == 0) {
			removeDevicesForHubPort(controller, hubSlotId, port);
			return;
		}

		if (hubHasChildOnPort(controller, hubSlotId, port)) {
			return;
		}

		if (hub->depth >= 5 or port > 15) {
			printf("XHCI: Hub slot=%u port=%u cannot hotplug route depth=%u.", hubSlotId, port, hub->depth);
			fflush(stdout);

			return;
		}

		if (!resetHubPort(controller, *hub, port, status)) {
			printf("XHCI: Hub slot=%u port=%u hotplug reset failed status=0x%04x.", hubSlotId, port, status);
			fflush(stdout);

			return;
		}

		const uint32_t childRouteString = hub->routeString | (static_cast<uint32_t>(port) << (hub->depth * 4));
		const uint8_t childSpeed = hubPortSpeed(status);
		char location[32] {};

		snprintf(location, sizeof(location), "hub%u-port%u", hubSlotId, port);
		enumerateDevice(controller, hub->controllerId, hub->rootPort, childRouteString, static_cast<uint8_t>(hub->depth + 1), childSpeed, hubSlotId, port, location);
	}

	void handleRootPortChange(MappedController &controller, const uint32_t port) {
		if (port == 0 or port > controller.maxPorts) {
			return;
		}

		const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
		const uint32_t portsc = mmioRead32(controller.operationalBase, offset);

		mmioWrite32(controller.operationalBase, offset, portsc & XHCI_PORTSC_CHANGE_BITS);

		if ((portsc & XHCI_PORTSC_CCS) == 0) {
			removeDevicesForRootPort(controller, static_cast<uint8_t>(port));
			return;
		}

		const bool alreadyKnown = ranges::any_of(controller.devices, [&](const XhciDevice &device) {
			return device.rootPort == port and device.depth == 0;
		});

		if (alreadyKnown) {
			return;
		}

		if (!resetPortIfNeeded(controller, static_cast<uint8_t>(port))) {
			return;
		}

		char location[16] {};
		snprintf(location, sizeof(location), "port%u", port);
		enumerateDevice(controller, controller.controllerId, static_cast<uint8_t>(port), 0, 0, portSpeed(controller, static_cast<uint8_t>(port)), 0, 0, location);
	}

	auto findEndpointByAddress(XhciDevice &device, const uint8_t endpointAddress) -> UsbEndpoint * {
		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
				if (endpoint.address == endpointAddress) {
					return &endpoint;
				}
			}
		}

		return nullptr;
	}

	void submitHubInterruptTransfer(MappedController &controller, XhciDevice &hub) {
		if (!hub.isHub or hub.hubInterruptEndpointAddress == 0 or hub.hubInterruptTransferPending) {
			return;
		}

		auto *endpoint = findEndpointByAddress(hub, hub.hubInterruptEndpointAddress);

		if (endpoint == nullptr or endpoint->transferRing.phys == 0) {
			return;
		}

		if (hub.hubInterruptBuffer.phys == 0 and !allocatePage(hub.hubInterruptBuffer)) {
			printf("XHCI: Failed to allocate hub interrupt buffer slot=%u.", hub.slotId);
			fflush(stdout);

			return;
		}

		memset(reinterpret_cast<void *>(hub.hubInterruptBuffer.virt), 0, XHCI_PAGE_SIZE);

		const uint32_t bitmapLength = max<uint32_t>(1, (hub.hubPortCount + 1 + 7) / 8);
		auto trb = XhciTrb();

		trb.parameterLow = static_cast<uint32_t>(hub.hubInterruptBuffer.phys);
		trb.parameterHigh = static_cast<uint32_t>(hub.hubInterruptBuffer.phys >> 32);
		trb.status = bitmapLength;
		trb.control = XHCI_TRB_ISP | XHCI_TRB_IOC | XHCI_TRB_DIR_IN | (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT);

		{
			const scoped_lock lock(eventRingMutex);

			if (!enqueueEndpointTrb(*endpoint, trb)) {
				return;
			}

			ringDoorbell(controller, hub.slotId, endpoint->endpointId);
		}

		hub.hubInterruptTransferPending = true;
	}

	void handleHubInterruptTransfer(MappedController &controller, const uint8_t slotId, const uint8_t endpointId) {
		auto *hub = findDeviceBySlot(controller, slotId);

		if (hub == nullptr or !hub->isHub or hub->hubInterruptEndpointId != endpointId) {
			return;
		}

		hub->hubInterruptTransferPending = false;

		if (hub->hubInterruptBuffer.virt != 0) {
			const auto *bytes = reinterpret_cast<uint8_t *>(hub->hubInterruptBuffer.virt);
			vector<uint8_t> changedPorts;

			for (uint8_t port = 1; port <= hub->hubPortCount; ++port) {
				if ((bytes[port / 8] & (1U << (port % 8))) != 0) {
					changedPorts.push_back(port);
				}
			}

			for (const uint8_t port : changedPorts) {
				handleHubPortChange(controller, slotId, port);
			}
		}

		hub = findDeviceBySlot(controller, slotId);

		if (hub != nullptr) {
			submitHubInterruptTransfer(controller, *hub);
		}
	}

	void drainPendingHubInterruptEvents(MappedController &controller) {
		vector<pair<uint8_t, uint8_t>> pending;

		{
			const scoped_lock lock(eventRingMutex);

			for (auto it = controller.pendingTransferEvents.begin(); it != controller.pendingTransferEvents.end();) {
				const auto slotId = static_cast<uint8_t>((it->control >> 24) & 0xFFU);
				const auto endpointId = static_cast<uint8_t>((it->control >> 16) & 0x1FU);
				const auto *device = findDeviceBySlot(controller, slotId);

				if (device == nullptr or !device->isHub or device->hubInterruptEndpointId != endpointId) {
					++it;
					continue;
				}

				pending.emplace_back(slotId, endpointId);
				it = controller.pendingTransferEvents.erase(it);
			}
		}

		for (const auto &[slotId, endpointId] : pending) {
			handleHubInterruptTransfer(controller, slotId, endpointId);
		}
	}

	void pollHubChanges(MappedController &controller) {
		drainPendingHubInterruptEvents(controller);

		vector<uint8_t> hubSlots;

		for (const auto &device : controller.devices) {
			if (device.isHub and device.configured) {
				hubSlots.push_back(device.slotId);
			}
		}

		for (const uint8_t hubSlotId : hubSlots) {
			auto *hub = findDeviceBySlot(controller, hubSlotId);

			if (hub == nullptr) {
				continue;
			}

			vector<uint8_t> changedPorts;

			for (uint8_t port = 1; port <= hub->hubPortCount; ++port) {
				uint16_t status = 0;
				uint16_t change = 0;

				if (!hubPortStatus(controller, *hub, port, status, change)) {
					continue;
				}

				if (change != 0 and ranges::find(changedPorts, port) == changedPorts.end()) {
					changedPorts.push_back(port);
				}
			}

			for (const uint8_t port : changedPorts) {
				handleHubPortChange(controller, hubSlotId, port);
			}

			hub = findDeviceBySlot(controller, hubSlotId);

			if (hub != nullptr) {
				submitHubInterruptTransfer(controller, *hub);
			}
		}
	}

	void pollRootPortChanges(MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
			const uint32_t portsc = mmioRead32(controller.operationalBase, offset);

			if ((portsc & XHCI_PORTSC_CHANGE_BITS) != 0) {
				handleRootPortChange(controller, port);
			}
		}
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

			uint32_t drained = 0;
			vector<uint32_t> changedPorts;
			vector<pair<uint8_t, uint8_t>> hubTransferCompletions;

			{
				const scoped_lock lock(eventRingMutex);
				for (;;) {
					const auto *events = reinterpret_cast<XhciTrb *>(controller->memory.eventRing.virt);
					const auto &event = events[controller->memory.eventDequeueIndex];

					if ((event.control & XHCI_TRB_CYCLE) != controller->memory.eventConsumerCycle) {
						break;
					}

					const uint32_t type = eventType(event);
					const uint32_t code = completionCode(event);
					const uint64_t eventParam = static_cast<uint64_t>(event.parameterLow) | (static_cast<uint64_t>(event.parameterHigh) << 32);

					if (type == XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT) {
						const uint32_t portId = static_cast<uint32_t>(eventParam >> 24);
						printf("XHCI: Port status change event for port %u code=%u.", portId, code);
						fflush(stdout);
						changedPorts.push_back(portId);
					} else if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
						const auto eventSlot = static_cast<uint8_t>((event.control >> 24) & 0xFFU);
						const auto eventEndpoint = static_cast<uint8_t>((event.control >> 16) & 0x1FU);
						hubTransferCompletions.emplace_back(eventSlot, eventEndpoint);
					} else if (loggedEvents < 32) {
						printf("XHCI: Deferred event type=%u code=%u ctrl=0x%x status=0x%x.", type, code, event.control, event.status);
						fflush(stdout);
						++loggedEvents;
					}

					controller->memory.eventDequeueIndex++;
					++drained;

					if (controller->memory.eventDequeueIndex == XHCI_EVENT_RING_TRBS) {
						controller->memory.eventDequeueIndex = 0;
						controller->memory.eventConsumerCycle ^= 1;
					}

					updateEventDequeuePointer(*controller);
				}

				if (drained != 0) {
					acknowledgeEvents(*controller);
				}
			}

			for (const uint32_t portId : changedPorts) {
				handleRootPortChange(*controller, portId);
			}

			for (const auto &[slotId, endpointId] : hubTransferCompletions) {
				handleHubInterruptTransfer(*controller, slotId, endpointId);
			}

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

	auto bulkTransferCallback(const void *ctx, const XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, const uint32_t pageCount, const uint32_t length, const bool in, uint32_t *actualLength) -> bool {
		(void) ctx;

		if (activeControllers == nullptr or device.controllerId >= activeControllers->size()) {
			return false;
		}

		auto *controller = &(*activeControllers)[device.controllerId];

		return controller != nullptr and bulkOrInterruptTransfer(*controller, device, endpoint, pagePhysArray, pageCount, length, in, actualLength);
	}

	auto resetBulkOnlyCallback(const void */*ctx*/, XhciDevice &device, const uint8_t interfaceNumber) -> bool {
		if (activeControllers == nullptr or device.controllerId >= activeControllers->size()) {
			return false;
		}

		auto &controller = (*activeControllers)[device.controllerId];
		return controlTransferNoData(controller, device, 0x21, USB_MASS_STORAGE_REQUEST_BULK_ONLY_RESET, 0, interfaceNumber);
	}

	auto clearEndpointHaltCallback(const void */*ctx*/, XhciDevice &device, UsbEndpoint &endpoint) -> bool {
		if (activeControllers == nullptr or device.controllerId >= activeControllers->size()) {
			return false;
		}

		auto &controller = (*activeControllers)[device.controllerId];
		const bool cleared = controlTransferNoData(controller, device, 0x02, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, endpoint.address);
		recoverEndpoint(controller, device, endpoint);
		return cleared;
	}

	[[noreturn]] auto storageReadHandler(void */*unused*/) -> void * {
		auto data = UsbStorageReadMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		uint64_t types[256] {};

		for (uint64_t i = 0; i < 256; ++i) {
			types[i] = USB_STORAGE_READ_MSG_BASE + i;
		}

		auto filter = filter_options();

		filter.whiteListTypes = types;
		filter.whiteListCount = 256;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(xhciPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = UsbStorageReadReplyMsgData();

			reply.requestId = data.requestId;
			reply.pageCount = data.pageCount;

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				const scoped_lock lock(usbStorageMutex);
				reply.success = massStorageDriver.read(data.controllerId, data.nsid, data.lba, data.pagePhysArray, data.pageCount);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = msg.type - USB_STORAGE_READ_MSG_BASE + USB_STORAGE_REPLY_READ_MSG_BASE;
			replyMsg.port = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(xhciPort, replyMsg.port, &replyMsg);
		}
	}

	[[noreturn]] auto storageWriteHandler(void */*unused*/) -> void * {
		auto data = UsbStorageWriteMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		uint64_t types[256] {};

		for (uint64_t i = 0; i < 256; ++i) {
			types[i] = USB_STORAGE_WRITE_MSG_BASE + i;
		}

		auto filter = filter_options();

		filter.whiteListTypes = types;
		filter.whiteListCount = 256;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(xhciPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = UsbStorageWriteReplyMsgData();

			reply.requestId = data.requestId;

			if (data.pageCount > 0 and data.pageCount <= STORAGE_MAX_PAGES_PER_MSG) {
				const scoped_lock lock(usbStorageMutex);
				reply.success = massStorageDriver.write(data.controllerId, data.nsid, data.lba, data.pagePhysArray, data.pageCount);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = msg.type - USB_STORAGE_WRITE_MSG_BASE + USB_STORAGE_REPLY_WRITE_MSG_BASE;
			replyMsg.port = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(xhciPort, replyMsg.port, &replyMsg);
		}
	}

	[[noreturn]] auto storageFlushHandler(void */*unused*/) -> void * {
		auto data = UsbStorageFlushMsgData();
		auto msg = hos_msg();

		msg.buffer = &data;
		msg.length = sizeof(data);

		uint64_t types[256] {};

		for (uint64_t i = 0; i < 256; ++i) {
			types[i] = USB_STORAGE_FLUSH_MSG_BASE + i;
		}

		auto filter = filter_options();

		filter.whiteListTypes = types;
		filter.whiteListCount = 256;

		for (;;) {
			memset(&data, 0, sizeof(data));

			if (receive_horizonos_message(xhciPort, &msg, &filter) != 0) {
				continue;
			}

			auto reply = UsbStorageFlushReplyMsgData();

			reply.requestId = data.requestId;

			{
				const scoped_lock lock(usbStorageMutex);
				reply.success = massStorageDriver.flush(data.controllerId, data.nsid);
			}

			auto replyMsg = hos_msg();

			replyMsg.type = msg.type - USB_STORAGE_FLUSH_MSG_BASE + USB_STORAGE_REPLY_FLUSH_MSG_BASE;
			replyMsg.port = data.replyPort != 0 ? data.replyPort : msg.src_port;
			replyMsg.buffer = &reply;
			replyMsg.length = sizeof(reply);

			send_horizonos_message(xhciPort, replyMsg.port, &replyMsg);
		}
	}

	void startStorageHandlers() {
		pthread_t readThread {};
		pthread_t writeThread {};
		pthread_t flushThread {};

		pthread_create(&readThread, nullptr, storageReadHandler, nullptr);
		pthread_create(&writeThread, nullptr, storageWriteHandler, nullptr);
		pthread_create(&flushThread, nullptr, storageFlushHandler, nullptr);
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

	auto registerUsbBlockDevice(const void */*ctx*/, const uint32_t controllerId, const uint32_t nsid, const uint64_t blockCount, const uint32_t blockSize, const char *name, uint64_t &deviceId) -> bool {
		if (storagePort == 0 or storageReplyPort == 0 or name == nullptr) {
			return false;
		}

		auto data = StorageRegisterBlockDeviceMsgData();

		data.driverPort = xhciPort;
		data.controllerId = controllerId;
		data.nsid = nsid;
		data.blockCount = blockCount;
		data.blockSize = blockSize;
		data.maxPagesPerRequest = STORAGE_MAX_PAGES_PER_MSG;
		data.transport = STORAGE_TRANSPORT_GENERIC_BLOCK;
		data.readMsgBase = USB_STORAGE_READ_MSG_BASE;
		data.writeMsgBase = USB_STORAGE_WRITE_MSG_BASE;
		data.flushMsgBase = USB_STORAGE_FLUSH_MSG_BASE;
		data.readReplyMsgBase = USB_STORAGE_REPLY_READ_MSG_BASE;
		data.writeReplyMsgBase = USB_STORAGE_REPLY_WRITE_MSG_BASE;
		data.flushReplyMsgBase = USB_STORAGE_REPLY_FLUSH_MSG_BASE;

		const string deviceName(name);

		fillName(data.name, sizeof(data.name), data.nameLength, deviceName);

		auto msg = hos_msg();

		msg.type = STORAGE_REGISTER_BLOCK_DEVICE_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storageReplyPort, storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageRegisterBlockDeviceReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(storageReplyPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		if (ret == 0 and reply.success) {
			deviceId = reply.deviceId;
			return true;
		}

		return false;
	}

	auto unregisterUsbBlockDevice(const void */*ctx*/, const uint64_t deviceId, const uint32_t controllerId, const uint32_t nsid) -> bool {
		if (storagePort == 0 or storageReplyPort == 0) {
			return false;
		}

		auto data = StorageUnregisterBlockDeviceMsgData();

		data.deviceId = deviceId;
		data.driverPort = xhciPort;
		data.controllerId = controllerId;
		data.nsid = nsid;

		auto msg = hos_msg();

		msg.type = STORAGE_UNREGISTER_BLOCK_DEVICE_MSG_TYPE;
		msg.port = storagePort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(storageReplyPort, storagePort, &msg) != 0) {
			return false;
		}

		auto reply = StorageUnregisterBlockDeviceReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { STORAGE_UNREGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		const int ret = receive_horizonos_message(storageReplyPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return ret == 0 and reply.success;
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
		const auto version = static_cast<uint16_t>(firstCapDword >> 16);
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

	const GetReplyMsgData storageInfo = waitForService("StorageManager");

	storagePort = storageInfo.port;

	if (register_horizonos_port(reinterpret_cast<long *>(&storageReplyPort)) != 0 or storageReplyPort == 0) {
		printf("XHCI: Failed to register Storage reply port.");
		fflush(stdout);

		return 1;
	}

	printf("XHCI: Storage info: Port: %lu, TID: %u, Version: %u.%u.%u.", storageInfo.port, storageInfo.tid, storageInfo.versionMajor, storageInfo.versionMinor, storageInfo.versionPatch);
	fflush(stdout);

	auto transport = UsbMassStorageTransport();

	transport.ctx = nullptr;
	transport.bulkTransfer = bulkTransferCallback;
	transport.registerBlockDevice = registerUsbBlockDevice;
	transport.unregisterBlockDevice = unregisterUsbBlockDevice;
	transport.resetBulkOnly = resetBulkOnlyCallback;
	transport.clearEndpointHalt = clearEndpointHaltCallback;

	massStorageDriver.setTransport(transport);

	startStorageHandlers();

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

	activeControllers = &controllers;

	for (const auto &device : devices) {
		controllers.emplace_back();

		auto &controller = controllers.back();
		const size_t controllerIndex = controllers.size() - 1;
		controller.controllerId = static_cast<uint32_t>(controllerIndex);

		if (!mapBar0(device, controller)) {
			controllers.pop_back();
			continue;
		}

		if (!bringUpController(controller, controllerIndex)) {
			munmap_extra(reinterpret_cast<void *>(controller.mmioVirt), controller.barSize, false);
			pciWrite32(device, PCI_COMMAND, controller.originalCommand);

			controllers.pop_back();
			
			continue;
		}
		auto &activeController = controller;

		setInterrupterEnabled(activeController, false);

		if (!startController(activeController.operationalBase, false)) {
			printf("XHCI: Controller %zu failed to start.", controllerIndex);
			fflush(stdout);

			continue;
		}

		printf("XHCI: Controller %zu started, configured %u device slot(s).", controllerIndex, activeController.configuredSlots);
		fflush(stdout);

		postStartProbe(activeController);

		enumerateRootPorts(activeController, static_cast<uint32_t>(controllerIndex));

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
		for (auto &controller : controllers) {
			pollRootPortChanges(controller);
			pollHubChanges(controller);
		}

		usleep(1000000);
	}
}
