#include "Xhci.hpp"
#include "MassStorage.hpp"

#include "horizonos/generic.h"
#include "horizonos/syscall.h"
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
}

void XhciUtils::fillName(char *dst, const size_t dstSize, size_t &length, const string &name) {
		const size_t copyLen = min(dstSize - 1, name.size());
		memcpy(dst, name.data(), copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	auto XhciUtils::mmioRead8(const uint64_t base, const uint32_t offset) -> uint8_t {
		return *reinterpret_cast<volatile uint8_t *>(base + offset);
	}

	auto XhciUtils::mmioRead32(const uint64_t base, const uint32_t offset) -> uint32_t {
		return *reinterpret_cast<volatile uint32_t *>(base + offset);
	}

	void XhciUtils::mmioWrite32(const uint64_t base, const uint32_t offset, const uint32_t value) {
		*reinterpret_cast<volatile uint32_t *>(base + offset) = value;
	}

	void XhciUtils::mmioWrite64(const uint64_t base, const uint32_t offset, const uint64_t value) {
		*reinterpret_cast<volatile uint64_t *>(base + offset) = value;
	}

		void XhciUtils::dmaReadFence() {
			__sync_synchronize();
		}

		void XhciUtils::dmaWriteFence() {
		__sync_synchronize();
	}

	auto XhciUtils::allocatePage(AllocatedPage &page, const uint64_t maxPhysExclusive) -> bool {
			const int allocResult = maxPhysExclusive == 0
				? allocPhysPage(&page.phys)
				: syscall(SYSCALL_ALLOC_PHYS_PAGE, reinterpret_cast<long *>(&page.phys), maxPhysExclusive);

			if (allocResult != 0) {
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

	void XhciUtils::freePage(AllocatedPage &page) {
		if (page.virt != 0) {
			munmap_extra(reinterpret_cast<void *>(page.virt), XHCI_PAGE_SIZE, false);
		}

		if (page.phys != 0) {
			freePhysPage(page.phys);
		}

		page = {};
	}

	void XhciUtils::releaseControllerMemory(ControllerMemory &memory) {
		for (auto &scratchpad : memory.scratchpads) {
			XhciUtils::freePage(scratchpad);
		}

		memory.scratchpads.clear();

		XhciUtils::freePage(memory.scratchpadArray);
		XhciUtils::freePage(memory.erst);
		XhciUtils::freePage(memory.eventRing);
		XhciUtils::freePage(memory.commandRing);
		XhciUtils::freePage(memory.dcbaa);
	}

	auto XhciUtils::registerWithNameRegistry() -> bool {
		auto data = RegisterMsgData();

		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());

		XhciUtils::fillName(data.name, sizeof(data.name), data.nameLength, "XHCI");

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

	auto XhciUtils::waitForService(const char *name) -> GetReplyMsgData {
		for (;;) {
			auto check = CheckMsgData();

			XhciUtils::fillName(check.name, sizeof(check.name), check.nameLength, name);

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

		XhciUtils::fillName(get.name, sizeof(get.name), get.nameLength, name);

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

	auto XhciUtils::pciRead32(const PciDevice &dev, const uint16_t offset) -> uint32_t {
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

	void XhciUtils::pciWrite32(const PciDevice &dev, const uint16_t offset, const uint32_t value) {
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

	void XhciUtils::msixGlobalEnable(const PciDevice &dev) {
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

	auto XhciUtils::msixAllocVector(const PciDevice &dev, const uint16_t tableIndex, const uint64_t notifyPort, const uint64_t lapicId) -> uint8_t {
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

	auto XhciUtils::msiAllocVector(const PciDevice &dev, const uint64_t notifyPort, const uint64_t lapicId) -> uint8_t {
		auto data = PciMsiAllocMsgData();

		data.bus = dev.bus;
		data.dev = dev.device;
		data.func = dev.function;
		data.port = notifyPort;
		data.lapicId = lapicId;

		auto msg = hos_msg();

		msg.type = PCI_MSI_ALLOC_MSG_TYPE;
		msg.port = pciPort;
		msg.buffer = &data;
		msg.length = sizeof(data);

		send_horizonos_message(xhciPort, pciPort, &msg);

		auto reply = PciMsiAllocReplyMsgData();
		auto recv = hos_msg();

		recv.buffer = &reply;
		recv.length = sizeof(reply);

		auto filter = filter_options();

		filter.whiteListTypes = new uint64_t[1] { PCI_MSI_ALLOC_REPLY_MSG_TYPE };
		filter.whiteListCount = 1;

		receive_horizonos_message(xhciPort, &recv, &filter);

		delete[] filter.whiteListTypes;

		return reply.vec;
	}

	auto XhciUtils::findControllers() -> vector<PciDevice> {
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

	auto XhciUtils::mapBar0(const PciDevice &dev, MappedController &controller) -> bool {
		const uint32_t bar0Lo = XhciUtils::pciRead32(dev, PCI_BAR0);

		if ((bar0Lo & 0x1U) != 0) {
			printf("XHCI: BAR0 for %02x:%02x.%x is I/O space, skipping.", dev.bus, dev.device, dev.function);
			fflush(stdout);

			return false;
		}

		const bool is64Bit = (bar0Lo & 0x6U) == 0x4U;
		const uint32_t bar0Hi = is64Bit ? XhciUtils::pciRead32(dev, PCI_BAR0 + 4) : 0;
		const uint64_t barPhys = is64Bit ? (static_cast<uint64_t>(bar0Hi) << 32) | (bar0Lo & ~0xFULL) : (bar0Lo & ~0xFULL);

		const uint32_t originalCommand = XhciUtils::pciRead32(dev, PCI_COMMAND);
		XhciUtils::pciWrite32(dev, PCI_COMMAND, originalCommand & ~PCI_COMMAND_MEMORY_SPACE);
		XhciUtils::pciWrite32(dev, PCI_BAR0, 0xFFFFFFFF);

		if (is64Bit) {
			XhciUtils::pciWrite32(dev, PCI_BAR0 + 4, 0xFFFFFFFF);
		}

		const uint32_t sizeLo = XhciUtils::pciRead32(dev, PCI_BAR0) & ~0xFU;
		const uint32_t sizeHi = is64Bit ? XhciUtils::pciRead32(dev, PCI_BAR0 + 4) : 0;

		XhciUtils::pciWrite32(dev, PCI_BAR0, bar0Lo);

		if (is64Bit) {
			XhciUtils::pciWrite32(dev, PCI_BAR0 + 4, bar0Hi);
		}

			const uint64_t sizeMask = is64Bit ? (static_cast<uint64_t>(sizeHi) << 32) | sizeLo : sizeLo;
			uint64_t barSize = 0x10000;

			if (sizeMask != 0) {
				barSize = is64Bit ? (~sizeMask + 1) : static_cast<uint64_t>(~sizeLo + 1U);
			}

		if (barSize == 0 or barSize > 0x10000000) {
			printf("XHCI: Unreasonable BAR0 size 0x%lx for %02x:%02x.%x.", barSize, dev.bus, dev.device, dev.function);
			fflush(stdout);

			XhciUtils::pciWrite32(dev, PCI_COMMAND, originalCommand);

			return false;
		}

		if (barSize < 0x1000) {
			barSize = 0x1000;
		}

		XhciUtils::pciWrite32(dev, PCI_COMMAND, originalCommand | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

		uint64_t mmioVirt = 0;

		if (mmap_phys(barPhys, barSize, &mmioVirt, false, MMapCacheMode::MAP_CACHE_UC) != 0) {
			printf("XHCI: Failed to map BAR0 phys=0x%lx size=0x%lx for %02x:%02x.%x.", barPhys, barSize, dev.bus, dev.device, dev.function);
			fflush(stdout);

			XhciUtils::pciWrite32(dev, PCI_COMMAND, originalCommand);

			return false;
		}

		controller.pci = dev;
		controller.barPhys = barPhys;
		controller.barSize = barSize;
		controller.mmioVirt = mmioVirt;
		controller.originalCommand = originalCommand;

		return true;
	}

	void XhciUtils::takeBiosOwnership(const uint64_t capabilityBase, const uint32_t hccParams1) {
		uint32_t offset = ((hccParams1 >> 16) & 0xFFFFU) * 4;

		for (uint32_t guard = 0; offset != 0 and guard < 64; ++guard) {
			const uint32_t cap = XhciUtils::mmioRead32(capabilityBase, offset);
			const uint32_t capId = cap & 0xFFU;
			const uint32_t nextOffset = ((cap >> 8) & 0xFFU) * 4;

			if (capId == XHCI_EXT_CAP_USB_LEGACY_SUPPORT) {
				uint32_t legacy = XhciUtils::mmioRead32(capabilityBase, offset);

				if ((legacy & XHCI_LEGACY_BIOS_OWNED) != 0) {
					XhciUtils::mmioWrite32(capabilityBase, offset, legacy | XHCI_LEGACY_OS_OWNED);

					for (uint32_t i = 0; i < 1000; ++i) {
						legacy = XhciUtils::mmioRead32(capabilityBase, offset);

						if ((legacy & XHCI_LEGACY_BIOS_OWNED) == 0 and (legacy & XHCI_LEGACY_OS_OWNED) != 0) {
							break;
						}

						usleep(1000);
					}
				} else if ((legacy & XHCI_LEGACY_OS_OWNED) == 0) {
					XhciUtils::mmioWrite32(capabilityBase, offset, legacy | XHCI_LEGACY_OS_OWNED);
					legacy = XhciUtils::mmioRead32(capabilityBase, offset);
				}

				XhciUtils::mmioWrite32(capabilityBase, offset + 4, 0);

				printf("XHCI: Legacy handoff cap=0x%x final=0x%x.", offset, legacy);
				fflush(stdout);

				return;
			}

				if (nextOffset == 0) {
					break;
				}

				offset += nextOffset;
			}
		}

		void XhciUtils::discoverRootPortProtocols(MappedController &controller, const uint64_t capabilityBase, const uint32_t hccParams1) {
			controller.rootPortProtocolMajor.assign(controller.maxPorts + 1, 0);
			uint32_t offset = ((hccParams1 >> 16) & 0xFFFFU) * 4;

			for (uint32_t guard = 0; offset != 0 and guard < 64; ++guard) {
				const uint32_t header = XhciUtils::mmioRead32(capabilityBase, offset);
				const uint32_t capId = header & 0xFFU;
				const uint32_t nextOffset = ((header >> 8) & 0xFFU) * 4;

				if (capId == XHCI_EXT_CAP_SUPPORTED_PROTOCOL) {
					const uint8_t major = static_cast<uint8_t>(header >> 24);
					const uint32_t ports = XhciUtils::mmioRead32(capabilityBase, offset + 8);
					const uint8_t firstPort = static_cast<uint8_t>(ports & 0xFFU);
					const uint8_t portCount = static_cast<uint8_t>((ports >> 8) & 0xFFU);

					for (uint32_t port = firstPort; port < static_cast<uint32_t>(firstPort) + portCount and port <= controller.maxPorts; ++port) {
						if (port != 0) {
							controller.rootPortProtocolMajor[port] = major;
						}
					}
				}

				if (nextOffset == 0) {
					break;
				}

				offset += nextOffset;
			}
		}

	auto XhciUtils::waitForControllerReady(const uint64_t operationalBase) -> bool {
		for (int i = 0; i < 10000; ++i) {
			if ((XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_CNR) == 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::haltController(const uint64_t operationalBase) -> bool {
		uint32_t command = XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command &= ~XHCI_USBCMD_RUN;
		XhciUtils::mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH) != 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::resetController(const uint64_t operationalBase) -> bool {
		uint32_t command = XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command |= XHCI_USBCMD_HCRST;
		XhciUtils::mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBCMD) & XHCI_USBCMD_HCRST) == 0) {
				return XhciUtils::waitForControllerReady(operationalBase);
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::startController(const uint64_t operationalBase, const bool interruptsEnabled) -> bool {
		uint32_t command = XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBCMD);
		command |= XHCI_USBCMD_RUN;

		if (interruptsEnabled) {
			command |= XHCI_USBCMD_INTE;
		} else {
			command &= ~XHCI_USBCMD_INTE;
		}

		XhciUtils::mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);

		for (int i = 0; i < 10000; ++i) {
			if ((XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBSTS) & XHCI_USBSTS_HCH) == 0) {
				return true;
			}

			usleep(1000);
		}

		return false;
	}

	void XhciUtils::setControllerInterruptsEnabled(const uint64_t operationalBase, const bool enabled) {
		uint32_t command = XhciUtils::mmioRead32(operationalBase, XHCI_OP_USBCMD);

		if (enabled) {
			command |= XHCI_USBCMD_INTE;
		} else {
			command &= ~XHCI_USBCMD_INTE;
		}

		XhciUtils::mmioWrite32(operationalBase, XHCI_OP_USBCMD, command);
	}

		void XhciUtils::powerRootPorts(MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
			const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

			if ((portsc & XHCI_PORTSC_PP) != 0) {
				continue;
			}

				XhciUtils::mmioWrite32(controller.operationalBase, offset, XHCI_PORTSC_PP | (portsc & XHCI_PORTSC_CHANGE_BITS));
			usleep(20000);

			const uint32_t powered = XhciUtils::mmioRead32(controller.operationalBase, offset);
			printf("XHCI: Powered root port %u PORTSC 0x%x -> 0x%x.", port, portsc, powered);
			fflush(stdout);
		}
	}

	auto XhciUtils::maxScratchpadBuffers(const uint32_t hcsParams2) -> uint32_t {
		return ((hcsParams2 >> 27) & 0x1FU) << 5U | ((hcsParams2 >> 21) & 0x1FU);
	}

		auto XhciUtils::setupScratchpads(ControllerMemory &memory, const uint32_t maxScratchpads, const uint64_t dmaAddressLimit) -> bool {
		memory.maxScratchpads = maxScratchpads;

		if (maxScratchpads == 0) {
			return true;
		}

			if (!XhciUtils::allocatePage(memory.scratchpadArray, dmaAddressLimit)) {
			return false;
		}

		auto *dcbaa = reinterpret_cast<uint64_t *>(memory.dcbaa.virt);
		auto *scratchpadArray = reinterpret_cast<uint64_t *>(memory.scratchpadArray.virt);
		dcbaa[0] = memory.scratchpadArray.phys;
		memory.scratchpads.reserve(maxScratchpads);

		for (uint32_t i = 0; i < maxScratchpads; ++i) {
			auto page = AllocatedPage();

				if (!XhciUtils::allocatePage(page, dmaAddressLimit)) {
				return false;
			}

			scratchpadArray[i] = page.phys;
			memory.scratchpads.push_back(page);
		}

		XhciUtils::dmaWriteFence();

		return true;
	}

		auto XhciUtils::setupCommandRing(ControllerMemory &memory, const uint64_t operationalBase, const uint64_t dmaAddressLimit) -> bool {
			if (!XhciUtils::allocatePage(memory.commandRing, dmaAddressLimit)) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(memory.commandRing.virt);
		auto &link = ring[XHCI_COMMAND_RING_TRBS - 1];
		link.parameterLow = static_cast<uint32_t>(memory.commandRing.phys);
		link.parameterHigh = static_cast<uint32_t>(memory.commandRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		XhciUtils::dmaWriteFence();
		XhciUtils::mmioWrite64(operationalBase, XHCI_OP_CRCR, memory.commandRing.phys | XHCI_TRB_CYCLE);

		return true;
	}

		auto XhciUtils::setupEventRing(ControllerMemory &memory, const uint64_t runtimeBase, const uint64_t dmaAddressLimit) -> bool {
			if (!XhciUtils::allocatePage(memory.eventRing, dmaAddressLimit)) {
			return false;
		}

			if (!XhciUtils::allocatePage(memory.erst, dmaAddressLimit)) {
			return false;
		}

		auto *erst = reinterpret_cast<XhciErstEntry *>(memory.erst.virt);

		erst[0].ringSegmentBase = memory.eventRing.phys;
		erst[0].ringSegmentSize = XHCI_EVENT_RING_TRBS;

		XhciUtils::dmaWriteFence();

		const uint64_t interrupterBase = runtimeBase + 0x20;

		XhciUtils::mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, 0);
		XhciUtils::mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMOD, 0);
		XhciUtils::mmioWrite32(interrupterBase, XHCI_INTERRUPTER_ERSTSZ, XHCI_ERST_ENTRIES);
		XhciUtils::mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERSTBA, memory.erst.phys);
		XhciUtils::mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERDP, memory.eventRing.phys | XHCI_ERDP_EHB);

		return true;
	}

	void XhciUtils::setInterrupterEnabled(const MappedController &controller, const bool enabled) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;

		XhciUtils::mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, enabled ? 0x3 : 0);
	}

	void XhciUtils::acknowledgeEvents(const MappedController &controller) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;
		const uint32_t iman = XhciUtils::mmioRead32(interrupterBase, XHCI_INTERRUPTER_IMAN);

		XhciUtils::mmioWrite32(interrupterBase, XHCI_INTERRUPTER_IMAN, (iman & 0x2U) | 0x1U);
		XhciUtils::mmioWrite32(controller.operationalBase, XHCI_OP_USBSTS, XHCI_USBSTS_EINT);
	}

	void XhciUtils::updateEventDequeuePointer(const MappedController &controller) {
		const uint64_t interrupterBase = controller.runtimeBase + 0x20;
		const uint64_t eventDequeuePhys = controller.memory.eventRing.phys + (controller.memory.eventDequeueIndex * sizeof(XhciTrb));

		XhciUtils::mmioWrite64(interrupterBase, XHCI_INTERRUPTER_ERDP, eventDequeuePhys | XHCI_ERDP_EHB);
	}

	auto XhciUtils::eventType(const XhciTrb &event) -> uint32_t {
		return (event.control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
	}

	auto XhciUtils::completionCode(const XhciTrb &event) -> uint32_t {
		return (event.status >> 24) & 0xFFU;
	}

	void XhciUtils::ringDoorbell(const MappedController &controller, const uint32_t target, const uint32_t value) {
		XhciUtils::dmaWriteFence();
		XhciUtils::mmioWrite32(controller.doorbellBase, target * 4, value);
	}

	auto XhciUtils::enqueueCommand(MappedController &controller, const XhciTrb &command) -> bool {
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

		XhciUtils::ringDoorbell(controller, 0, 0);

		return true;
	}

	auto XhciUtils::submitNoopCommand(MappedController &controller) -> bool {
		auto command = XhciTrb();
		command.control = XHCI_TRB_TYPE_NOOP_COMMAND << XHCI_TRB_TYPE_SHIFT;

		return XhciUtils::enqueueCommand(controller, command);
	}

	auto XhciUtils::commandPhys(const MappedController &controller, const uint32_t index) -> uint64_t {
		return controller.memory.commandRing.phys + (index * sizeof(XhciTrb));
	}

	auto XhciUtils::enqueueCommandAndGetPhys(MappedController &controller, const XhciTrb &command, uint64_t &phys) -> bool {
		const uint32_t index = controller.memory.commandEnqueueIndex;

		if (!XhciUtils::enqueueCommand(controller, command)) {
			return false;
		}

		phys = XhciUtils::commandPhys(controller, index);

		return true;
	}

	auto XhciUtils::waitForCommandCompletion(MappedController &controller, const uint64_t commandTrbPhys, XhciTrb &completion, const int timeoutMs) -> bool {
		uint32_t ignoredLogs = 32;

		for (int i = 0; i < timeoutMs; ++i) {
			const auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);

			for (;;) {
				const auto &event = events[controller.memory.eventDequeueIndex];

					if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
						break;
					}

					XhciUtils::dmaReadFence();

				const uint32_t type = XhciUtils::eventType(event);
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
					printf("XHCI: Ignored event type=%u code=%u while waiting for command 0x%lx.", type, XhciUtils::completionCode(event), commandTrbPhys);
					fflush(stdout);

					--ignoredLogs;
				}

				controller.memory.eventDequeueIndex++;

				if (controller.memory.eventDequeueIndex == XHCI_EVENT_RING_TRBS) {
					controller.memory.eventDequeueIndex = 0;
					controller.memory.eventConsumerCycle ^= 1;
				}

				XhciUtils::updateEventDequeuePointer(controller);

				if (matched) {
					XhciUtils::acknowledgeEvents(controller);

					return true;
				}
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::runCommand(MappedController &controller, const XhciTrb &command, XhciTrb &completion, const int timeoutMs) -> bool {
		unique_lock lock(eventRingMutex);
		uint64_t phys = 0;

		if (!XhciUtils::enqueueCommandAndGetPhys(controller, command, phys)) {
			return false;
		}

		if (!XhciUtils::waitForCommandCompletion(controller, phys, completion, timeoutMs)) {
			printf("XHCI: Timed out waiting for command TRB 0x%lx.", phys);
			fflush(stdout);

			return false;
		}

		const uint32_t code = XhciUtils::completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS) {
			printf("XHCI: Command TRB 0x%lx failed code=%u ctrl=0x%x status=0x%x.", phys, code, completion.control, completion.status);
			fflush(stdout);

			return false;
		}

		return true;
	}

	auto XhciUtils::drainEvents(MappedController &controller, uint32_t &loggedEvents) -> uint32_t {
		unique_lock lock(eventRingMutex);
		uint32_t drained = 0;

		for (;;) {
			const auto *events = reinterpret_cast<XhciTrb *>(controller.memory.eventRing.virt);
			const auto &event = events[controller.memory.eventDequeueIndex];

			if ((event.control & XHCI_TRB_CYCLE) != controller.memory.eventConsumerCycle) {
				break;
			}

			XhciUtils::dmaReadFence();

			const uint32_t type = XhciUtils::eventType(event);
			const uint32_t code = XhciUtils::completionCode(event);

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

			XhciUtils::updateEventDequeuePointer(controller);
		}

		if (drained != 0) {
			XhciUtils::acknowledgeEvents(controller);
		}

		return drained;
	}

	void XhciUtils::logControllerStatus(const MappedController &controller, const char *phase) {
		const uint32_t usbCmd = XhciUtils::mmioRead32(controller.operationalBase, XHCI_OP_USBCMD);
		const uint32_t usbSts = XhciUtils::mmioRead32(controller.operationalBase, XHCI_OP_USBSTS);
		const uint32_t iman = XhciUtils::mmioRead32(controller.runtimeBase + 0x20, XHCI_INTERRUPTER_IMAN);

		printf("XHCI: %s status USBCMD=0x%x USBSTS=0x%x IMAN=0x%x.", phase, usbCmd, usbSts, iman);
		fflush(stdout);
	}

	void XhciUtils::logPorts(const MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portOffset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
			const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, portOffset);

			if (portsc != 0) {
				printf("XHCI: Port %u PORTSC=0x%x.", port, portsc);
				fflush(stdout);
			}
		}
	}

	void XhciUtils::logPortState(const MappedController &controller, const uint8_t port, const char *phase) {
		if (port == 0 or port > controller.maxPorts) {
			return;
		}

		const uint32_t portOffset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
		const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, portOffset);
		const uint32_t speed = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK;

		printf("XHCI: %s port=%u PORTSC=0x%x ccs=%u ped=%u pr=%u speed=%u changes=0x%x.",
		       phase,
		       port,
		       portsc,
		       (portsc & XHCI_PORTSC_CCS) != 0,
		       (portsc & XHCI_PORTSC_PED) != 0,
		       (portsc & XHCI_PORTSC_PR) != 0,
		       speed,
		       portsc & XHCI_PORTSC_CHANGE_BITS);
		fflush(stdout);
	}

	void XhciUtils::logDeviceContext(const MappedController &controller, const XhciDevice &device, const char *phase) {
		if (device.deviceContext.virt == 0) {
			return;
		}

		const uint32_t slot0 = XhciUtils::contextDword(device.deviceContext, controller, 0, 0);
		const uint32_t slot1 = XhciUtils::contextDword(device.deviceContext, controller, 0, 1);
		const uint32_t ep0State = XhciUtils::endpointState(controller, device, 1);
		const uint32_t ep0Dword0 = XhciUtils::contextDword(device.deviceContext, controller, 1, 0);
		const uint32_t ep0Dword1 = XhciUtils::contextDword(device.deviceContext, controller, 1, 1);
		const uint32_t ep0Dword2 = XhciUtils::contextDword(device.deviceContext, controller, 1, 2);
		const uint32_t ep0Dword3 = XhciUtils::contextDword(device.deviceContext, controller, 1, 3);
		const uint32_t ep0Dword4 = XhciUtils::contextDword(device.deviceContext, controller, 1, 4);

		printf("XHCI: %s slot=%u slotCtx[0]=0x%x slotCtx[1]=0x%x ep0State=%u ep0Ctx[0..4]=0x%x,0x%x,0x%x,0x%x,0x%x.",
		       phase,
		       device.slotId,
		       slot0,
		       slot1,
		       ep0State,
		       ep0Dword0,
		       ep0Dword1,
		       ep0Dword2,
		       ep0Dword3,
		       ep0Dword4);
		fflush(stdout);
	}

	void XhciUtils::postStartProbe(MappedController &controller) {
		uint32_t loggedEvents = 0;

		XhciUtils::logControllerStatus(controller, "post-start");
		XhciUtils::logPorts(controller);

		if (!XhciUtils::submitNoopCommand(controller)) {
			printf("XHCI: Failed to submit No-Op command.");
			fflush(stdout);

			return;
		}

		printf("XHCI: Submitted No-Op command.");
		fflush(stdout);

		for (int i = 0; i < 100; ++i) {
			const uint32_t drained = XhciUtils::drainEvents(controller, loggedEvents);

			if (drained != 0) {
				printf("XHCI: Polled %u event(s) after No-Op command.", drained);
				fflush(stdout);

				return;
			}

			usleep(1000);
		}

		XhciUtils::logControllerStatus(controller, "after No-Op timeout");

		printf("XHCI: No event observed after No-Op command.");
		fflush(stdout);
	}

	auto XhciUtils::contextSize(const MappedController &controller) -> uint32_t {
		return controller.uses64ByteContexts ? XHCI_CONTEXT_SIZE_64 : XHCI_CONTEXT_SIZE_32;
	}

	auto XhciUtils::contextPtr(const AllocatedPage &page, const MappedController &controller, const uint32_t index) -> uint32_t * {
		return reinterpret_cast<uint32_t *>(page.virt + index * XhciUtils::contextSize(controller));
	}

	auto XhciUtils::contextDword(const AllocatedPage &page, const MappedController &controller, const uint32_t index, const uint32_t dword) -> uint32_t {
		return XhciUtils::contextPtr(page, controller, index)[dword];
	}

	void XhciUtils::setContextDword(const AllocatedPage &page, const MappedController &controller, const uint32_t index, const uint32_t dword, const uint32_t value) {
		XhciUtils::contextPtr(page, controller, index)[dword] = value;
	}

	auto XhciUtils::endpointState(const MappedController &controller, const XhciDevice &device, const uint8_t endpointId) -> uint32_t {
		if (device.deviceContext.virt == 0) {
			return XHCI_ENDPOINT_STATE_DISABLED;
		}

		return XhciUtils::contextDword(device.deviceContext, controller, endpointId, 0) & 0x7U;
	}

	auto XhciUtils::portSpeed(const MappedController &controller, const uint8_t port) -> uint8_t {
		const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC);

		return static_cast<uint8_t>((portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
	}

		auto XhciUtils::ep0MaxPacketForSpeed(const MappedController &controller, const uint8_t rootPort, const uint8_t speed) -> uint16_t {
			const bool usb3Port = rootPort < controller.rootPortProtocolMajor.size() and controller.rootPortProtocolMajor[rootPort] >= 3;

			if (usb3Port or speed >= 4) {
				return 512;
		}

		if (speed == 3) {
			return 64;
		}

			return 8;
		}

		auto XhciUtils::endpointInterval(const uint8_t speed, const uint8_t attributes, const uint8_t interval) -> uint8_t {
			const uint8_t transferType = attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK;

			if (transferType != USB_ENDPOINT_TRANSFER_INTERRUPT and transferType != USB_ENDPOINT_TRANSFER_ISOCHRONOUS) {
				return 0;
			}

			if (interval == 0) {
				return 0;
			}

			if (speed >= 3) {
				return static_cast<uint8_t>(min<uint32_t>(interval, 16) - 1);
			}

			if (transferType == USB_ENDPOINT_TRANSFER_ISOCHRONOUS) {
				return static_cast<uint8_t>(min<uint32_t>(interval + 2U, 15));
			}

			const uint32_t microframes = static_cast<uint32_t>(interval) * 8;
			uint8_t exponent = 0;
			uint32_t period = 1;

			while (period < microframes and exponent < 15) {
				period <<= 1;
				++exponent;
			}

			return exponent;
		}

	auto XhciUtils::usbEndpointId(const uint8_t endpointAddress) -> uint8_t {
		const uint8_t endpointNumber = endpointAddress & 0x0FU;
		const bool in = (endpointAddress & 0x80U) != 0;

		return static_cast<uint8_t>((endpointNumber * 2) + (in ? 1 : 0));
	}

	auto XhciUtils::xhciEndpointType(const uint8_t endpointAddress, const uint8_t attributes) -> uint8_t {
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

	auto XhciUtils::contextIndexForEndpointId(const uint8_t endpointId) -> uint32_t {
		return static_cast<uint32_t>(endpointId) + 1;
	}

	auto XhciUtils::waitForTransferEvent(MappedController &controller, const uint8_t slotId, const uint8_t endpointId, XhciTrb &completion, const int timeoutMs, const bool logTimeoutPath) -> bool {
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

					XhciUtils::dmaReadFence();

				const uint32_t type = XhciUtils::eventType(event);
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
					       XhciUtils::completionCode(event),
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

				XhciUtils::updateEventDequeuePointer(controller);

				if (matched) {
					XhciUtils::acknowledgeEvents(controller);
					return true;
				}
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::enqueueTransferTrb(XhciDevice &device, const XhciTrb &trb) -> bool {
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

	auto XhciUtils::enqueueEndpointTrb(UsbEndpoint &endpoint, const XhciTrb &trb) -> bool {
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

		auto XhciUtils::bulkOrInterruptTransfer(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, const uint32_t pageCount, const uint32_t length, const bool in, uint32_t *actualLength, const int timeoutMs, const bool logTimeout) -> bool {
		if (actualLength != nullptr) {
			*actualLength = 0;
		}

			if (endpoint.transferRing.phys == 0 or pagePhysArray == nullptr or pageCount == 0 or length == 0) {
				return false;
			}

			for (uint32_t page = 0; page < pageCount; ++page) {
				if ((pagePhysArray[page] & (XHCI_PAGE_SIZE - 1)) != 0) {
					return false;
				}
			}

			struct BouncePageList {
				vector<AllocatedPage> pages {};

				~BouncePageList() {
					for (auto &page : pages) {
						XhciUtils::freePage(page);
					}
				}
			} bounce;

			vector<uint64_t> dmaPages;
			const uint64_t *transferPages = pagePhysArray;
			bool needsBounce = false;
			uint32_t checkedLength = length;

			if (controller.dmaAddressLimit != 0) {
				for (uint32_t page = 0; page < pageCount and checkedLength != 0; ++page) {
					const uint32_t chunk = min<uint32_t>(checkedLength, XHCI_PAGE_SIZE);
					const uint64_t phys = pagePhysArray[page];

					if (phys >= controller.dmaAddressLimit or chunk > controller.dmaAddressLimit - phys) {
						needsBounce = true;
						break;
					}

					checkedLength -= chunk;
				}
			}

			if (needsBounce) {
				bounce.pages.reserve(pageCount);
				dmaPages.reserve(pageCount);
				uint32_t bounceRemaining = length;

				for (uint32_t page = 0; page < pageCount and bounceRemaining != 0; ++page) {
					const uint32_t chunk = min<uint32_t>(bounceRemaining, XHCI_PAGE_SIZE);
					auto bouncePage = AllocatedPage();

					if (!XhciUtils::allocatePage(bouncePage, controller.dmaAddressLimit)) {
						return false;
					}

					if (!in) {
						uint64_t sourceVirt = 0;

						if (mmap_phys(pagePhysArray[page], XHCI_PAGE_SIZE, &sourceVirt, false) != 0) {
							XhciUtils::freePage(bouncePage);
							return false;
						}

						memcpy(reinterpret_cast<void *>(bouncePage.virt), reinterpret_cast<const void *>(sourceVirt), chunk);
						munmap_extra(reinterpret_cast<void *>(sourceVirt), XHCI_PAGE_SIZE, false);
					}

					dmaPages.push_back(bouncePage.phys);
					bounce.pages.push_back(bouncePage);
					bounceRemaining -= chunk;
				}

				if (bounceRemaining != 0) {
					return false;
				}

				transferPages = dmaPages.data();
			}

			unique_lock lock(eventRingMutex);
		uint32_t remaining = length;

		for (uint32_t page = 0; page < pageCount and remaining != 0; ++page) {
			const uint32_t chunk = min<uint32_t>(remaining, XHCI_PAGE_SIZE);
			auto trb = XhciTrb();

				trb.parameterLow = static_cast<uint32_t>(transferPages[page]);
				trb.parameterHigh = static_cast<uint32_t>(transferPages[page] >> 32);
			trb.status = chunk;
			trb.control = XHCI_TRB_ISP | (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT);

				remaining -= chunk;

			if (remaining == 0) {
				trb.control |= XHCI_TRB_IOC;
			} else {
				trb.control |= XHCI_TRB_CHAIN;
			}

			if (!XhciUtils::enqueueEndpointTrb(endpoint, trb)) {
				return false;
			}
		}

		if (remaining != 0) {
			return false;
		}

		XhciUtils::ringDoorbell(controller, device.slotId, endpoint.endpointId);

		auto completion = XhciTrb();

		if (!XhciUtils::waitForTransferEvent(controller, device.slotId, endpoint.endpointId, completion, timeoutMs, logTimeout)) {
			if (logTimeout) {
				printf("XHCI: Endpoint transfer timed out slot=%u ep=0x%02x.", device.slotId, endpoint.address);
				fflush(stdout);
			}

			lock.unlock();
			XhciUtils::recoverEndpoint(controller, device, endpoint);

			return false;
		}

		const uint32_t code = XhciUtils::completionCode(completion);

		if (code != XHCI_COMPLETION_SUCCESS and code != XHCI_COMPLETION_SHORT_PACKET) {
			printf("XHCI: Endpoint transfer failed slot=%u ep=0x%02x code=%u status=0x%x.", device.slotId, endpoint.address, code, completion.status);
			fflush(stdout);

			return false;
		}

		const uint32_t residue = completion.status & 0xFFFFFFU;

			if (actualLength != nullptr) {
				*actualLength = length >= residue ? length - residue : 0;
			}

			const uint32_t completedLength = length >= residue ? length - residue : 0;

			if (needsBounce and in) {
				uint32_t copyRemaining = completedLength;

				for (uint32_t page = 0; page < bounce.pages.size() and copyRemaining != 0; ++page) {
					const uint32_t chunk = min<uint32_t>(copyRemaining, XHCI_PAGE_SIZE);
					uint64_t destinationVirt = 0;

					if (mmap_phys(pagePhysArray[page], XHCI_PAGE_SIZE, &destinationVirt, false) != 0) {
						return false;
					}

					memcpy(reinterpret_cast<void *>(destinationVirt), reinterpret_cast<const void *>(bounce.pages[page].virt), chunk);
					munmap_extra(reinterpret_cast<void *>(destinationVirt), XHCI_PAGE_SIZE, false);
					copyRemaining -= chunk;
				}

				if (copyRemaining != 0) {
					return false;
				}
			}

			return true;
	}

	auto XhciUtils::controlTransferIn(MappedController &controller, XhciDevice &device, const uint8_t requestType, const uint8_t request, const uint16_t value, const uint16_t index, const uint16_t length, const uint64_t dataPhys) -> bool {
		for (uint32_t attempt = 0; attempt < XHCI_CONTROL_TRANSFER_ATTEMPTS; ++attempt) {
			unique_lock lock(eventRingMutex);
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

			if (!XhciUtils::enqueueTransferTrb(device, setup) or !XhciUtils::enqueueTransferTrb(device, data) or !XhciUtils::enqueueTransferTrb(device, status)) {
				return false;
			}

			XhciUtils::ringDoorbell(controller, device.slotId, 1);

			auto completion = XhciTrb();

			if (!XhciUtils::waitForTransferEvent(controller, device.slotId, 1, completion, 1000)) {
				printf("XHCI: Control transfer timed out slot=%u attempt=%u.", device.slotId, attempt + 1);
				fflush(stdout);

				lock.unlock();
				XhciUtils::recoverControlEndpoint(controller, device);
				usleep(10000);

				continue;
			}

			const uint32_t code = XhciUtils::completionCode(completion);

			if (code == XHCI_COMPLETION_SUCCESS or code == XHCI_COMPLETION_SHORT_PACKET) {
				return true;
			}

			const uint64_t failedTrb = static_cast<uint64_t>(completion.parameterLow) | (static_cast<uint64_t>(completion.parameterHigh) << 32);

			printf("XHCI: Control transfer failed slot=%u code=%u ctrl=0x%x status=0x%x trb=0x%lx state=%u attempt=%u.", device.slotId, code, completion.control, completion.status, failedTrb, XhciUtils::endpointState(controller, device, 1), attempt + 1);
			fflush(stdout);
			XhciUtils::logPortState(controller, device.rootPort, "control-fail");
			XhciUtils::logDeviceContext(controller, device, "control-fail");

			lock.unlock();
			XhciUtils::recoverControlEndpoint(controller, device);
			usleep(10000);
		}

		return false;
	}

	auto XhciUtils::controlTransferNoData(MappedController &controller, XhciDevice &device, const uint8_t requestType, const uint8_t request, const uint16_t value, const uint16_t index) -> bool {
		for (uint32_t attempt = 0; attempt < XHCI_CONTROL_TRANSFER_ATTEMPTS; ++attempt) {
			unique_lock lock(eventRingMutex);
			auto setup = XhciTrb();

			setup.parameterLow = static_cast<uint32_t>(requestType) | (static_cast<uint32_t>(request) << 8) | (static_cast<uint32_t>(value) << 16);
			setup.parameterHigh = static_cast<uint32_t>(index);
			setup.status = 8;
			setup.control = XHCI_TRB_IDT | (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT);

			auto status = XhciTrb();

			status.control = XHCI_TRB_DIR_IN | XHCI_TRB_IOC | (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT);

			if (!XhciUtils::enqueueTransferTrb(device, setup) or !XhciUtils::enqueueTransferTrb(device, status)) {
				return false;
			}

			XhciUtils::ringDoorbell(controller, device.slotId, 1);

			auto completion = XhciTrb();

			if (!XhciUtils::waitForTransferEvent(controller, device.slotId, 1, completion, 1000)) {
				printf("XHCI: Control no-data transfer timed out slot=%u request=%u attempt=%u.", device.slotId, request, attempt + 1);
				fflush(stdout);

				lock.unlock();
				XhciUtils::recoverControlEndpoint(controller, device);
				usleep(10000);

				continue;
			}

			const uint32_t code = XhciUtils::completionCode(completion);

			if (code == XHCI_COMPLETION_SUCCESS) {
				return true;
			}

			const uint64_t failedTrb = static_cast<uint64_t>(completion.parameterLow) | (static_cast<uint64_t>(completion.parameterHigh) << 32);

			printf("XHCI: Control no-data transfer failed slot=%u request=%u code=%u ctrl=0x%x status=0x%x trb=0x%lx state=%u attempt=%u.", device.slotId, request, code, completion.control, completion.status, failedTrb, XhciUtils::endpointState(controller, device, 1), attempt + 1);
			fflush(stdout);
			XhciUtils::logPortState(controller, device.rootPort, "control-no-data-fail");
			XhciUtils::logDeviceContext(controller, device, "control-no-data-fail");

			lock.unlock();
			XhciUtils::recoverControlEndpoint(controller, device);
			usleep(10000);
		}

		return false;
	}

		auto XhciUtils::waitForPortReset(const MappedController &controller, const uint8_t port, const bool warmReset) -> bool {
			const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
			const uint32_t resetBit = warmReset ? XHCI_PORTSC_WPR : XHCI_PORTSC_PR;
			const uint32_t completionBit = warmReset ? XHCI_PORTSC_WRC : XHCI_PORTSC_PRC;

			for (int i = 0; i < 1000; ++i) {
				const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

				if ((portsc & resetBit) == 0 and (portsc & completionBit) != 0) {
					XhciUtils::mmioWrite32(controller.operationalBase, offset, (portsc & XHCI_PORTSC_PP) | (portsc & XHCI_PORTSC_CHANGE_BITS));

				return true;
			}

			usleep(1000);
		}

		return false;
	}

	auto XhciUtils::resetPortIfNeeded(const MappedController &controller, const uint8_t port) -> bool {
		const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
		uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

		XhciUtils::logPortState(controller, port, "reset-check");

			if ((portsc & XHCI_PORTSC_CCS) == 0) {
				return false;
			}

			const uint8_t speed = static_cast<uint8_t>((portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
			const bool protocolIsUsb3 = port < controller.rootPortProtocolMajor.size() and controller.rootPortProtocolMajor[port] >= 3;
			const bool warmReset = protocolIsUsb3 or speed >= 4;

			if ((portsc & XHCI_PORTSC_PED) != 0) {
				if (warmReset) {
					XhciUtils::mmioWrite32(controller.operationalBase, offset, (portsc & XHCI_PORTSC_PP) | (portsc & XHCI_PORTSC_CHANGE_BITS));
				usleep(50000);
				XhciUtils::logPortState(controller, port, "reset-skip-enabled");

				return true;
			}
		}

			XhciUtils::mmioWrite32(controller.operationalBase, offset, (portsc & XHCI_PORTSC_PP) | (warmReset ? XHCI_PORTSC_WPR : XHCI_PORTSC_PR));

			if (!XhciUtils::waitForPortReset(controller, port, warmReset)) {
				printf("XHCI: Port %u %s reset timed out.", port, warmReset ? "warm" : "normal");
			fflush(stdout);

			return false;
		}

		portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

		if ((portsc & XHCI_PORTSC_PED) == 0) {
			printf("XHCI: Port %u did not enable after reset PORTSC=0x%x.", port, portsc);
			fflush(stdout);

			return false;
		}

		usleep(50000);
		XhciUtils::logPortState(controller, port, "reset-done");

		return true;
	}

	auto XhciUtils::enableSlot(MappedController &controller, uint8_t &slotId) -> bool {
		auto command = XhciTrb();

		command.control = XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND << XHCI_TRB_TYPE_SHIFT;

		auto completion = XhciTrb();

		if (!XhciUtils::runCommand(controller, command, completion)) {
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

		auto XhciUtils::setupTransferRing(const MappedController &controller, XhciDevice &device) -> bool {
			if (!XhciUtils::allocatePage(device.transferRing, controller.dmaAddressLimit)) {
			return false;
		}

		auto *ring = reinterpret_cast<XhciTrb *>(device.transferRing.virt);
		auto &link = ring[XHCI_TRANSFER_RING_TRBS - 1];

		link.parameterLow = static_cast<uint32_t>(device.transferRing.phys);
		link.parameterHigh = static_cast<uint32_t>(device.transferRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		return true;
	}

		auto XhciUtils::setupEndpointTransferRing(const MappedController &controller, UsbEndpoint &endpoint) -> bool {
			if (!XhciUtils::allocatePage(endpoint.transferRing, controller.dmaAddressLimit)) {
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

	void XhciUtils::resetEndpointTransferRing(UsbEndpoint &endpoint) {
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

	auto XhciUtils::setupDeviceContexts(const MappedController &controller, XhciDevice &device) -> bool {
			if (!XhciUtils::allocatePage(device.inputContext, controller.dmaAddressLimit) or
			    !XhciUtils::allocatePage(device.deviceContext, controller.dmaAddressLimit) or
			    !XhciUtils::allocatePage(device.descriptorBuffer, controller.dmaAddressLimit) or
			    !XhciUtils::setupTransferRing(controller, device)) {
			return false;
		}

		auto *dcbaa = reinterpret_cast<uint64_t *>(controller.memory.dcbaa.virt);

		dcbaa[device.slotId] = device.deviceContext.phys;
		XhciUtils::dmaWriteFence();

		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, 0x3);

			const uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (1U << 27);
			const uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;
			const uint32_t slotDword2 = static_cast<uint32_t>(device.ttHubSlotId) | (static_cast<uint32_t>(device.ttPortNumber) << 8);

			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 2, slotDword2);

		const uint64_t dequeue = device.transferRing.phys | XHCI_TRB_CYCLE;
			const uint32_t ep0Dword1 = XHCI_EP0_DWORD1_DEFAULT | (static_cast<uint32_t>(device.maxPacketSize) << 16);

			XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 1, ep0Dword1);
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 2, static_cast<uint32_t>(dequeue));
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 3, static_cast<uint32_t>(dequeue >> 32));
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 4, 8U);
		XhciUtils::dmaWriteFence();

		return true;
	}

	auto XhciUtils::addressDevice(MappedController &controller, const XhciDevice &device) -> bool {
		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	auto XhciUtils::disableSlot(MappedController &controller, const XhciDevice &device) -> bool {
		if (device.slotId == 0) {
			return true;
		}

		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	auto XhciUtils::stopEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_STOP_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	auto XhciUtils::resetEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();

		command.control = (XHCI_TRB_TYPE_RESET_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	auto XhciUtils::setEndpointDequeuePointer(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool {
		auto command = XhciTrb();
		const uint64_t dequeue = endpoint.transferRing.phys | XHCI_TRB_CYCLE;

		command.parameterLow = static_cast<uint32_t>(dequeue);
		command.parameterHigh = static_cast<uint32_t>(dequeue >> 32);
		command.control = (XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER_COMMAND << XHCI_TRB_TYPE_SHIFT) |
		                  (static_cast<uint32_t>(endpoint.endpointId) << 16) |
		                  (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	void XhciUtils::recoverEndpoint(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint) {
		XhciUtils::stopEndpoint(controller, device, endpoint);
		XhciUtils::resetEndpoint(controller, device, endpoint);
		XhciUtils::resetEndpointTransferRing(endpoint);
		XhciUtils::setEndpointDequeuePointer(controller, device, endpoint);
	}

	void XhciUtils::resetControlTransferRing(XhciDevice &device) {
		if (device.transferRing.virt == 0) {
			return;
		}

		memset(reinterpret_cast<void *>(device.transferRing.virt), 0, XHCI_PAGE_SIZE);

		auto *ring = reinterpret_cast<XhciTrb *>(device.transferRing.virt);
		auto &link = ring[XHCI_TRANSFER_RING_TRBS - 1];

		link.parameterLow = static_cast<uint32_t>(device.transferRing.phys);
		link.parameterHigh = static_cast<uint32_t>(device.transferRing.phys >> 32);
		link.control = XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE | (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT);

		device.transferEnqueueIndex = 0;
		device.transferProducerCycle = 1;
	}

	void XhciUtils::recoverControlEndpoint(MappedController &controller, XhciDevice &device) {
		auto ep0 = UsbEndpoint();
		bool canSetDequeue = false;

		ep0.endpointId = 1;
		ep0.address = 0;
		ep0.transferRing = device.transferRing;
		ep0.transferEnqueueIndex = device.transferEnqueueIndex;
		ep0.transferProducerCycle = device.transferProducerCycle;

		const uint32_t state = XhciUtils::endpointState(controller, device, ep0.endpointId);

		if (state == XHCI_ENDPOINT_STATE_RUNNING) {
			canSetDequeue = XhciUtils::stopEndpoint(controller, device, ep0);
		} else if (state == XHCI_ENDPOINT_STATE_HALTED) {
			canSetDequeue = XhciUtils::resetEndpoint(controller, device, ep0);
		} else if (state == XHCI_ENDPOINT_STATE_STOPPED or state == XHCI_ENDPOINT_STATE_ERROR) {
			canSetDequeue = true;
		}

		if (canSetDequeue) {
			const uint32_t dequeueState = XhciUtils::endpointState(controller, device, ep0.endpointId);

			if (dequeueState == XHCI_ENDPOINT_STATE_STOPPED or dequeueState == XHCI_ENDPOINT_STATE_ERROR) {
				XhciUtils::resetControlTransferRing(device);

				ep0.transferEnqueueIndex = device.transferEnqueueIndex;
				ep0.transferProducerCycle = device.transferProducerCycle;
				XhciUtils::setEndpointDequeuePointer(controller, device, ep0);
			} else {
				printf("XHCI: EP0 recovery skipped dequeue reset slot=%u state=%u afterState=%u.", device.slotId, state, dequeueState);
				fflush(stdout);
			}
		} else {
			printf("XHCI: EP0 recovery command failed slot=%u state=%u.", device.slotId, state);
			fflush(stdout);
		}

		const scoped_lock lock(eventRingMutex);

		for (auto it = controller.pendingTransferEvents.begin(); it != controller.pendingTransferEvents.end();) {
			const auto eventSlot = static_cast<uint8_t>((it->control >> 24) & 0xFFU);
			const auto eventEndpoint = static_cast<uint8_t>((it->control >> 16) & 0x1FU);

			if (eventSlot == device.slotId and eventEndpoint == 1) {
				it = controller.pendingTransferEvents.erase(it);
			} else {
				++it;
			}
		}
	}

	void XhciUtils::releaseDeviceMemory(const MappedController &controller, XhciDevice &device) {
		if (device.slotId != 0 and controller.memory.dcbaa.virt != 0) {
			auto *dcbaa = reinterpret_cast<uint64_t *>(controller.memory.dcbaa.virt);

			dcbaa[device.slotId] = 0;
		}

		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
				XhciUtils::freePage(endpoint.transferRing);
			}
		}

		device.interfaces.clear();

		XhciUtils::freePage(device.descriptorBuffer);
		XhciUtils::freePage(device.hubInterruptBuffer);
		XhciUtils::freePage(device.transferRing);
		XhciUtils::freePage(device.deviceContext);
		XhciUtils::freePage(device.inputContext);

		device = {};
	}

	auto XhciUtils::evaluateEp0Context(MappedController &controller, const XhciDevice &device) -> bool {
		if (device.inputContext.virt == 0) {
			return false;
		}

		memset(reinterpret_cast<void *>(device.inputContext.virt), 0, XHCI_PAGE_SIZE);

		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, 0x3);

			const uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (1U << 27);
			const uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;
			const uint32_t slotDword2 = static_cast<uint32_t>(device.ttHubSlotId) | (static_cast<uint32_t>(device.ttPortNumber) << 8);

			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 2, slotDword2);

		const uint64_t dequeue = device.transferRing.phys | XHCI_TRB_CYCLE;
			const uint32_t ep0Dword1 = XHCI_EP0_DWORD1_DEFAULT | (static_cast<uint32_t>(device.maxPacketSize) << 16);

		XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 1, ep0Dword1);
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 2, static_cast<uint32_t>(dequeue));
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 3, static_cast<uint32_t>(dequeue >> 32));
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_EP0_CONTEXT_INDEX, 4, 8U);

		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_EVALUATE_CONTEXT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		return XhciUtils::runCommand(controller, command, completion, 1000);
	}

	auto XhciUtils::configureEndpoints(MappedController &controller, XhciDevice &device) -> bool {
		if (device.inputContext.virt == 0) {
			return false;
		}

		memset(reinterpret_cast<void *>(device.inputContext.virt), 0, XHCI_PAGE_SIZE);

		uint32_t addFlags = 0x1;
		uint8_t maxEndpointId = 1;

		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
					if (endpoint.transferRing.phys == 0 and !XhciUtils::setupEndpointTransferRing(controller, endpoint)) {
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

		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 0, 0x0);
		XhciUtils::setContextDword(device.inputContext, controller, XHCI_INPUT_CONTROL_CONTEXT_INDEX, 1, addFlags);

			uint32_t slotDword0 = (device.routeString & 0xFFFFFU) | (static_cast<uint32_t>(device.speed) << 20) | (static_cast<uint32_t>(maxEndpointId) << 27);
			uint32_t slotDword1 = static_cast<uint32_t>(device.rootPort) << 16;
			uint32_t slotDword2 = static_cast<uint32_t>(device.ttHubSlotId) | (static_cast<uint32_t>(device.ttPortNumber) << 8);

			if (device.isHub) {
				slotDword0 |= 1U << 26;

				if (device.multiTT) {
					slotDword0 |= 1U << 25;
				}

				slotDword1 |= static_cast<uint32_t>(device.hubPortCount) << 24;
				slotDword2 |= static_cast<uint32_t>((device.hubCharacteristics >> 5) & 0x3U) << 16;
			}

			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 0, slotDword0);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 1, slotDword1);
			XhciUtils::setContextDword(device.inputContext, controller, XHCI_SLOT_CONTEXT_INDEX, 2, slotDword2);

		for (auto &interface : device.interfaces) {
			for (const auto &endpoint : interface.endpoints) {
				const uint64_t dequeue = endpoint.transferRing.phys | XHCI_TRB_CYCLE;
				const uint32_t ctxIndex = XhciUtils::contextIndexForEndpointId(endpoint.endpointId);
					const uint32_t epDword0 = (static_cast<uint32_t>((endpoint.maxEsitPayload >> 16) & 0xFFU) << 24) |
					                          (static_cast<uint32_t>(XhciUtils::endpointInterval(device.speed, endpoint.attributes, endpoint.interval)) << 16) |
					                          (static_cast<uint32_t>(endpoint.mult & 0x3U) << 8);
					const uint32_t errorCount = (endpoint.attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_ISOCHRONOUS ? 0 : 3;
					const uint32_t epDword1 = (static_cast<uint32_t>(endpoint.maxPacketSize) << 16) |
					                          (static_cast<uint32_t>(endpoint.maxBurst) << 8) |
					                          (static_cast<uint32_t>(endpoint.endpointType) << 3) |
					                          (errorCount << 1);
					const uint32_t avgTrbLength = min<uint32_t>(endpoint.maxPacketSize, 0xFFFF);
					const uint32_t epDword4 = avgTrbLength | ((endpoint.maxEsitPayload & 0xFFFFU) << 16);

					XhciUtils::setContextDword(device.inputContext, controller, ctxIndex, 0, epDword0);
				XhciUtils::setContextDword(device.inputContext, controller, ctxIndex, 1, epDword1);
				XhciUtils::setContextDword(device.inputContext, controller, ctxIndex, 2, static_cast<uint32_t>(dequeue));
				XhciUtils::setContextDword(device.inputContext, controller, ctxIndex, 3, static_cast<uint32_t>(dequeue >> 32));
				XhciUtils::setContextDword(device.inputContext, controller, ctxIndex, 4, epDword4);
			}
		}

		auto command = XhciTrb();

		command.parameterLow = static_cast<uint32_t>(device.inputContext.phys);
		command.parameterHigh = static_cast<uint32_t>(device.inputContext.phys >> 32);
		command.control = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND << XHCI_TRB_TYPE_SHIFT) | (static_cast<uint32_t>(device.slotId) << 24);

		auto completion = XhciTrb();

		if (!XhciUtils::runCommand(controller, command, completion, 1000)) {
			printf("XHCI: Configure Endpoint failed slot=%u addFlags=0x%x.", device.slotId, addFlags);
			fflush(stdout);

			return false;
		}

		printf("XHCI: Configured endpoints slot=%u addFlags=0x%x.", device.slotId, addFlags);
		fflush(stdout);

		return true;
	}

	auto XhciUtils::readDeviceDescriptor(MappedController &controller, XhciDevice &device, const uint16_t length) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!XhciUtils::controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_DEVICE << 8, 0, length, device.descriptorBuffer.phys)) {
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

	auto XhciUtils::readStringDescriptor(MappedController &controller, XhciDevice &device, const uint8_t index) -> string {
		if (index == 0) {
			return {};
		}

		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!XhciUtils::controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, (USB_DESCRIPTOR_STRING << 8) | index, 0x0409, 255, device.descriptorBuffer.phys)) {
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

		void XhciUtils::parseConfigurationDescriptor(XhciDevice &device, const uint8_t *desc, const uint16_t totalLength) {
			uint16_t offset = 0;
			UsbInterface *currentInterface = nullptr;
			UsbEndpoint *currentEndpoint = nullptr;

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
					currentEndpoint = nullptr;
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
					const uint16_t rawMaxPacket = static_cast<uint16_t>(desc[offset + 4]) | (static_cast<uint16_t>(desc[offset + 5]) << 8);
					const uint16_t maxPacket = rawMaxPacket & 0x7FFU;
				const uint8_t interval = desc[offset + 6];
				auto endpoint = UsbEndpoint();

				endpoint.address = endpointAddress;
				endpoint.attributes = attributes;
				endpoint.maxPacketSize = maxPacket;
				endpoint.interval = interval;
					endpoint.endpointId = XhciUtils::usbEndpointId(endpointAddress);
					endpoint.endpointType = XhciUtils::xhciEndpointType(endpointAddress, attributes);

					if ((attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_INTERRUPT or
					    (attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_ISOCHRONOUS) {
						const uint8_t additionalTransactions = static_cast<uint8_t>(min<uint32_t>((rawMaxPacket >> 11) & 0x3U, 2));
						const uint32_t transactions = 1U + additionalTransactions;

						if (device.speed == 3) {
							endpoint.maxBurst = additionalTransactions;
						}

						endpoint.maxEsitPayload = maxPacket * transactions;
					}

					if (currentInterface != nullptr and endpoint.endpointId != 0 and endpoint.endpointType != 0) {
						currentInterface->endpoints.push_back(endpoint);
						currentEndpoint = &currentInterface->endpoints.back();
					} else {
						currentEndpoint = nullptr;
					}

				printf("XHCI: Endpoint slot=%u addr=0x%02x attrs=0x%02x maxPacket=%u interval=%u.",
				       device.slotId,
				       endpointAddress,
				       attributes,
				       maxPacket,
					       interval);
					fflush(stdout);
				} else if (type == USB_DESCRIPTOR_SS_ENDPOINT_COMPANION and length >= 6 and currentEndpoint != nullptr) {
					currentEndpoint->maxBurst = desc[offset + 2];

					if ((currentEndpoint->attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_ISOCHRONOUS) {
						currentEndpoint->mult = desc[offset + 3] & 0x3U;
					}

					if ((currentEndpoint->attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_INTERRUPT or
					    (currentEndpoint->attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_ISOCHRONOUS) {
						currentEndpoint->maxEsitPayload = static_cast<uint16_t>(desc[offset + 4]) |
						                                  (static_cast<uint16_t>(desc[offset + 5]) << 8);
					}
				}

			offset += length;
		}
	}

	auto XhciUtils::readConfigurationDescriptor(MappedController &controller, XhciDevice &device, uint8_t &configurationValue) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!XhciUtils::controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_CONFIGURATION << 8, 0, 9, device.descriptorBuffer.phys)) {
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

		if (!XhciUtils::controlTransferIn(controller, device, 0x80, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_CONFIGURATION << 8, 0, totalLength, device.descriptorBuffer.phys)) {
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

		XhciUtils::parseConfigurationDescriptor(device, desc, totalLength);

		return true;
	}

	auto XhciUtils::setConfiguration(MappedController &controller, XhciDevice &device, const uint8_t configurationValue) -> bool {
		if (!XhciUtils::controlTransferNoData(controller, device, 0x00, USB_REQUEST_SET_CONFIGURATION, configurationValue, 0)) {
			return false;
		}

		printf("XHCI: Set configuration slot=%u value=%u.", device.slotId, configurationValue);
		fflush(stdout);

		return true;
	}

	auto XhciUtils::enumerateDevice(MappedController &controller, const uint32_t controllerId, const uint8_t rootPort, const uint32_t routeString, const uint8_t depth, const uint8_t speed, const uint8_t parentSlotId, const uint8_t hubPort, const char *location) -> bool {
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

			if (parentSlotId != 0 and speed <= 2) {
				for (const auto &parent : controller.devices) {
					if (parent.slotId == parentSlotId and parent.speed == 3) {
						device.ttHubSlotId = parentSlotId;
						device.ttPortNumber = hubPort;
						break;
					}
				}
			}

			device.maxPacketSize = XhciUtils::ep0MaxPacketForSpeed(controller, rootPort, device.speed);

		if (!XhciUtils::enableSlot(controller, device.slotId)) {
			controller.devices.pop_back();

			return false;
		}

		if (!XhciUtils::setupDeviceContexts(controller, device)) {
			printf("XHCI: Failed to allocate contexts for %s slot %u.", location, device.slotId);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		if (!XhciUtils::addressDevice(controller, device)) {
			printf("XHCI: Failed to address device on %s slot %u.", location, device.slotId);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		printf("XHCI: Addressed device on %s as slot %u speed=%u route=0x%x.", location, device.slotId, device.speed, device.routeString);
		fflush(stdout);
		XhciUtils::logPortState(controller, device.rootPort, "addressed");
		XhciUtils::logDeviceContext(controller, device, "addressed");

		XhciUtils::resetControlTransferRing(device);
		XhciUtils::dmaWriteFence();

		usleep(50000);

		if (!XhciUtils::readDeviceDescriptor(controller, device, 8)) {
			printf("XHCI: Failed to read initial device descriptor slot=%u.", device.slotId);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		const auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
		const uint16_t actualMaxPacketSize = desc[7] == 9 ? 512 : desc[7];

		if (actualMaxPacketSize != device.maxPacketSize) {
			device.maxPacketSize = actualMaxPacketSize;

			if (!XhciUtils::evaluateEp0Context(controller, device)) {
				printf("XHCI: Failed to evaluate EP0 context slot=%u maxPacket=%u.", device.slotId, device.maxPacketSize);
				fflush(stdout);

				XhciUtils::disableSlot(controller, device);
				XhciUtils::releaseDeviceMemory(controller, device);

				controller.devices.pop_back();

				return false;
			}
		}

		if (XhciUtils::readDeviceDescriptor(controller, device, 18)) {
			const auto *fullDesc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
			const uint8_t manufacturerIndex = fullDesc[14];
			const uint8_t productIndex = fullDesc[15];
			const uint8_t serialIndex = fullDesc[16];

			device.manufacturer = XhciUtils::readStringDescriptor(controller, device, manufacturerIndex);
			device.product = XhciUtils::readStringDescriptor(controller, device, productIndex);
			device.serial = XhciUtils::readStringDescriptor(controller, device, serialIndex);

			if (!device.manufacturer.empty() or !device.product.empty() or !device.serial.empty()) {
				printf("XHCI: Device slot=%u strings manufacturer='%s' product='%s' serial='%s'.", device.slotId, device.manufacturer.c_str(), device.product.c_str(), device.serial.c_str());
				fflush(stdout);
			}
		}

		uint8_t configurationValue = 0;

		if (!XhciUtils::readConfigurationDescriptor(controller, device, configurationValue) or configurationValue == 0) {
			printf("XHCI: Failed to read usable configuration descriptor slot=%u.", device.slotId);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		XhciUtils::prepareHubMetadata(controller, device);

		if (!XhciUtils::configureEndpoints(controller, device)) {
			printf("XHCI: Failed to configure endpoints slot=%u.", device.slotId);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		if (!XhciUtils::setConfiguration(controller, device, configurationValue)) {
			printf("XHCI: Failed to set configuration slot=%u value=%u.", device.slotId, configurationValue);
			fflush(stdout);

			XhciUtils::disableSlot(controller, device);
			XhciUtils::releaseDeviceMemory(controller, device);

			controller.devices.pop_back();

			return false;
		}

		device.configurationValue = configurationValue;
		device.configured = true;

		XhciUtils::bindClassDrivers(controller, device, controllerId);

		return true;
	}

	auto XhciUtils::hubPortStatus(MappedController &controller, XhciDevice &hub, const uint8_t port, uint16_t &status, uint16_t &change) -> bool {
		memset(reinterpret_cast<void *>(hub.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!XhciUtils::controlTransferIn(controller, hub, 0xA3, USB_REQUEST_GET_STATUS, 0, port, 4, hub.descriptorBuffer.phys)) {
			return false;
		}

		const auto *bytes = reinterpret_cast<uint8_t *>(hub.descriptorBuffer.virt);

		status = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
		change = static_cast<uint16_t>(bytes[2]) | (static_cast<uint16_t>(bytes[3]) << 8);

		return true;
	}

	auto XhciUtils::hubPortSpeed(const uint16_t status) -> uint8_t {
		if ((status & USB_HUB_PORT_STATUS_HIGH_SPEED) != 0) {
			return 3;
		}

		if ((status & USB_HUB_PORT_STATUS_LOW_SPEED) != 0) {
			return 2;
		}

		return 1;
	}

	void XhciUtils::clearHubPortChangeBits(MappedController &controller, XhciDevice &hub, const uint8_t port, const uint16_t change) {
		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_CONNECTION - 16))) != 0) {
			XhciUtils::controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_CONNECTION, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_ENABLE - 16))) != 0) {
			XhciUtils::controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_ENABLE, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_OVER_CURRENT - 16))) != 0) {
			XhciUtils::controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_OVER_CURRENT, port);
		}

		if ((change & (1U << (USB_HUB_FEATURE_C_PORT_RESET - 16))) != 0) {
			XhciUtils::controlTransferNoData(controller, hub, 0x23, USB_REQUEST_CLEAR_FEATURE, USB_HUB_FEATURE_C_PORT_RESET, port);
		}
	}

	auto XhciUtils::resetHubPort(MappedController &controller, XhciDevice &hub, const uint8_t port, uint16_t &status) -> bool {
		uint16_t oldStatus = 0;
		uint16_t oldChange = 0;

		if (XhciUtils::hubPortStatus(controller, hub, port, oldStatus, oldChange)) {
			XhciUtils::clearHubPortChangeBits(controller, hub, port, oldChange);
		}

		XhciUtils::controlTransferNoData(controller, hub, 0x23, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_RESET, port);

		uint16_t change = 0;

		for (uint32_t attempt = 0; attempt < 100; ++attempt) {
			usleep(10000);

			if (!XhciUtils::hubPortStatus(controller, hub, port, status, change)) {
				continue;
			}

			if ((change & (1U << (USB_HUB_FEATURE_C_PORT_RESET - 16))) != 0) {
				XhciUtils::clearHubPortChangeBits(controller, hub, port, change);

				break;
			}
		}

		return (status & USB_HUB_PORT_STATUS_CONNECTION) != 0 and (status & USB_HUB_PORT_STATUS_ENABLE) != 0;
	}

	auto XhciUtils::readHubDescriptor(MappedController &controller, XhciDevice &device, const UsbInterface &interface) -> bool {
		memset(reinterpret_cast<void *>(device.descriptorBuffer.virt), 0, XHCI_PAGE_SIZE);

		if (!XhciUtils::controlTransferIn(controller, device, 0xA0, USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_HUB << 8, 0, 9, device.descriptorBuffer.phys)) {
			printf("XHCI: Hub descriptor read failed slot=%u if=%u.", device.slotId, interface.number);
			fflush(stdout);

			return false;
		}

		auto *desc = reinterpret_cast<uint8_t *>(device.descriptorBuffer.virt);
		const uint8_t portCount = desc[2];
		const uint16_t characteristics = static_cast<uint16_t>(desc[3]) | (static_cast<uint16_t>(desc[4]) << 8);
		const uint8_t powerOnDelayMs = static_cast<uint8_t>(min<uint32_t>(255, max<uint32_t>(20, static_cast<uint32_t>(desc[5]) * 2)));

			device.isHub = true;
			device.multiTT = interface.interfaceProtocol == 2;
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

	void XhciUtils::prepareHubMetadata(MappedController &controller, XhciDevice &device) {
		for (const auto &interface : device.interfaces) {
			if (interface.interfaceClass == USB_CLASS_HUB) {
				XhciUtils::readHubDescriptor(controller, device, interface);
				return;
			}
		}
	}

	void XhciUtils::probeHub(MappedController &controller, XhciDevice &device, const UsbInterface &interface) {
		if (!device.isHub and !XhciUtils::readHubDescriptor(controller, device, interface)) {
			return;
		}

		for (uint8_t port = 1; port <= device.hubPortCount; ++port) {
			XhciUtils::controlTransferNoData(controller, device, 0x23, USB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_POWER, port);
		}

		usleep(static_cast<useconds_t>(device.hubPowerOnDelayMs) * 1000);

		for (uint8_t port = 1; port <= device.hubPortCount; ++port) {
			uint16_t status = 0;
			uint16_t change = 0;

			if (!XhciUtils::hubPortStatus(controller, device, port, status, change)) {
				continue;
			}

			XhciUtils::clearHubPortChangeBits(controller, device, port, change);

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

			if (!XhciUtils::resetHubPort(controller, device, port, status)) {
				printf("XHCI: Hub slot=%u port=%u reset failed status=0x%04x.", device.slotId, port, status);
				fflush(stdout);

				continue;
			}

			const uint32_t childRouteString = device.routeString | (static_cast<uint32_t>(port) << (device.depth * 4));
			const uint8_t childSpeed = XhciUtils::hubPortSpeed(status);
			char location[32] {};

			snprintf(location, sizeof(location), "hub%u-port%u", device.slotId, port);
			XhciUtils::enumerateDevice(controller, device.controllerId, device.rootPort, childRouteString, static_cast<uint8_t>(device.depth + 1), childSpeed, device.slotId, port, location);
		}

		XhciUtils::submitHubInterruptTransfer(controller, device);
	}

	void XhciUtils::configureBootHid(MappedController &controller, XhciDevice &device, const UsbInterface &interface) {
		if (interface.interfaceSubclass != 1) {
			return;
		}

		const char *kind = interface.interfaceProtocol == 1 ? "keyboard" : (interface.interfaceProtocol == 2 ? "mouse" : "unknown");
		const bool protocolOk = XhciUtils::controlTransferNoData(controller, device, 0x21, USB_HID_REQUEST_SET_PROTOCOL, 0, interface.number);
		const bool idleOk = XhciUtils::controlTransferNoData(controller, device, 0x21, USB_HID_REQUEST_SET_IDLE, 0, interface.number);

		printf("XHCI/HID: Boot HID %s slot=%u if=%u endpointCount=%zu protocol=%d idle=%d input handling not installed.",
		       kind,
		       device.slotId,
		       interface.number,
		       interface.endpoints.size(),
		       protocolOk,
		       idleOk);
		fflush(stdout);
	}

	void XhciUtils::bindClassDrivers(MappedController &controller, XhciDevice &device, const uint32_t controllerId) {
		for (auto &interface : device.interfaces) {
			if (interface.interfaceClass == USB_CLASS_HID and interface.interfaceSubclass == 1) {
				XhciUtils::configureBootHid(controller, device, interface);
			} else if (interface.interfaceClass == USB_CLASS_HUB) {
				XhciUtils::probeHub(controller, device, interface);
			} else if (interface.interfaceClass == USB_CLASS_MASS_STORAGE and interface.interfaceSubclass == USB_SUBCLASS_SCSI and interface.interfaceProtocol == USB_PROTOCOL_BULK_ONLY) {
				massStorageDriver.bind(controllerId, device, interface);
			}
		}
	}

	void XhciUtils::enumerateRootPorts(MappedController &controller, const uint32_t controllerId) {
		controller.devices.reserve(max<uint32_t>(controller.maxPorts, controller.configuredSlots));

		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC);

			if ((portsc & XHCI_PORTSC_CCS) == 0) {
				continue;
			}

			if (!XhciUtils::resetPortIfNeeded(controller, port)) {
				continue;
			}

			char location[16] {};

			snprintf(location, sizeof(location), "port%u", port);
			XhciUtils::enumerateDevice(controller, controllerId, port, 0, 0, XhciUtils::portSpeed(controller, port), 0, 0, location);
		}

		printf("XHCI: Enumerated %zu root device(s).", controller.devices.size());
		fflush(stdout);
	}

	void XhciUtils::removeDevicesForRootPort(MappedController &controller, const uint8_t rootPort) {
		const scoped_lock storageLock(usbStorageMutex);

		for (auto it = controller.devices.begin(); it != controller.devices.end();) {
			if (it->rootPort != rootPort) {
				++it;
				continue;
			}

			printf("XHCI: Removing device slot=%u rootPort=%u route=0x%x.", it->slotId, it->rootPort, it->routeString);
			fflush(stdout);

			massStorageDriver.removeDevice(*it);
			XhciUtils::disableSlot(controller, *it);
			XhciUtils::releaseDeviceMemory(controller, *it);
			it = controller.devices.erase(it);
		}
	}

	void XhciUtils::removeDevicesForHubPort(MappedController &controller, const uint8_t parentSlotId, const uint8_t hubPort) {
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
				XhciUtils::disableSlot(controller, *it);
				XhciUtils::releaseDeviceMemory(controller, *it);
				it = controller.devices.erase(it);
				progress = true;
			}
		}
	}

	auto XhciUtils::findDeviceBySlot(MappedController &controller, const uint8_t slotId) -> XhciDevice * {
		for (auto &device : controller.devices) {
			if (device.slotId == slotId) {
				return &device;
			}
		}

		return nullptr;
	}

	auto XhciUtils::hubHasChildOnPort(const MappedController &controller, const uint8_t parentSlotId, const uint8_t hubPort) -> bool {
		return ranges::any_of(controller.devices, [&](const XhciDevice &device) {
			return device.parentSlotId == parentSlotId and device.hubPort == hubPort;
		});
	}

	void XhciUtils::handleHubPortChange(MappedController &controller, const uint8_t hubSlotId, const uint8_t port) {
		auto *hub = XhciUtils::findDeviceBySlot(controller, hubSlotId);

		if (hub == nullptr or port == 0 or port > hub->hubPortCount) {
			return;
		}

		uint16_t status = 0;
		uint16_t change = 0;

		if (!XhciUtils::hubPortStatus(controller, *hub, port, status, change)) {
			return;
		}

		XhciUtils::clearHubPortChangeBits(controller, *hub, port, change);

		if ((change & ((1U << (USB_HUB_FEATURE_C_PORT_CONNECTION - 16)) | (1U << (USB_HUB_FEATURE_C_PORT_ENABLE - 16)))) == 0) {
			return;
		}

		if ((status & USB_HUB_PORT_STATUS_CONNECTION) == 0) {
			XhciUtils::removeDevicesForHubPort(controller, hubSlotId, port);
			return;
		}

		if (XhciUtils::hubHasChildOnPort(controller, hubSlotId, port)) {
			return;
		}

		if (hub->depth >= 5 or port > 15) {
			printf("XHCI: Hub slot=%u port=%u cannot hotplug route depth=%u.", hubSlotId, port, hub->depth);
			fflush(stdout);

			return;
		}

		if (!XhciUtils::resetHubPort(controller, *hub, port, status)) {
			printf("XHCI: Hub slot=%u port=%u hotplug reset failed status=0x%04x.", hubSlotId, port, status);
			fflush(stdout);

			return;
		}

		const uint32_t childRouteString = hub->routeString | (static_cast<uint32_t>(port) << (hub->depth * 4));
		const uint8_t childSpeed = XhciUtils::hubPortSpeed(status);
		char location[32] {};

		snprintf(location, sizeof(location), "hub%u-port%u", hubSlotId, port);
		XhciUtils::enumerateDevice(controller, hub->controllerId, hub->rootPort, childRouteString, static_cast<uint8_t>(hub->depth + 1), childSpeed, hubSlotId, port, location);
	}

	void XhciUtils::handleRootPortChange(MappedController &controller, const uint32_t port) {
		if (port == 0 or port > controller.maxPorts) {
			return;
		}

		const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
		const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

		XhciUtils::mmioWrite32(controller.operationalBase, offset, (portsc & XHCI_PORTSC_PP) | (portsc & XHCI_PORTSC_CHANGE_BITS));

		if ((portsc & XHCI_PORTSC_CCS) == 0) {
			XhciUtils::removeDevicesForRootPort(controller, static_cast<uint8_t>(port));
			return;
		}

		const bool alreadyKnown = ranges::any_of(controller.devices, [&](const XhciDevice &device) {
			return device.rootPort == port and device.depth == 0;
		});

		if (alreadyKnown) {
			return;
		}

		if (!XhciUtils::resetPortIfNeeded(controller, static_cast<uint8_t>(port))) {
			return;
		}

		char location[16] {};
		snprintf(location, sizeof(location), "port%u", port);
		XhciUtils::enumerateDevice(controller, controller.controllerId, static_cast<uint8_t>(port), 0, 0, XhciUtils::portSpeed(controller, static_cast<uint8_t>(port)), 0, 0, location);
	}

	auto XhciUtils::findEndpointByAddress(XhciDevice &device, const uint8_t endpointAddress) -> UsbEndpoint * {
		for (auto &interface : device.interfaces) {
			for (auto &endpoint : interface.endpoints) {
				if (endpoint.address == endpointAddress) {
					return &endpoint;
				}
			}
		}

		return nullptr;
	}

	void XhciUtils::submitHubInterruptTransfer(MappedController &controller, XhciDevice &hub) {
		if (!hub.isHub or hub.hubInterruptEndpointAddress == 0 or hub.hubInterruptTransferPending) {
			return;
		}

		auto *endpoint = XhciUtils::findEndpointByAddress(hub, hub.hubInterruptEndpointAddress);

		if (endpoint == nullptr or endpoint->transferRing.phys == 0) {
			return;
		}

		if (hub.hubInterruptBuffer.phys == 0 and !XhciUtils::allocatePage(hub.hubInterruptBuffer, controller.dmaAddressLimit)) {
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
		trb.control = XHCI_TRB_ISP | XHCI_TRB_IOC | (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT);

		{
			const scoped_lock lock(eventRingMutex);

			if (!XhciUtils::enqueueEndpointTrb(*endpoint, trb)) {
				return;
			}

			XhciUtils::ringDoorbell(controller, hub.slotId, endpoint->endpointId);
		}

		hub.hubInterruptTransferPending = true;
	}

	void XhciUtils::handleHubInterruptTransfer(MappedController &controller, const uint8_t slotId, const uint8_t endpointId) {
		auto *hub = XhciUtils::findDeviceBySlot(controller, slotId);

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
				XhciUtils::handleHubPortChange(controller, slotId, port);
			}
		}

		hub = XhciUtils::findDeviceBySlot(controller, slotId);

		if (hub != nullptr) {
			XhciUtils::submitHubInterruptTransfer(controller, *hub);
		}
	}

	void XhciUtils::drainPendingHubInterruptEvents(MappedController &controller) {
		vector<pair<uint8_t, uint8_t>> pending;

		{
			const scoped_lock lock(eventRingMutex);

			for (auto it = controller.pendingTransferEvents.begin(); it != controller.pendingTransferEvents.end();) {
				const auto slotId = static_cast<uint8_t>((it->control >> 24) & 0xFFU);
				const auto endpointId = static_cast<uint8_t>((it->control >> 16) & 0x1FU);
				const auto *device = XhciUtils::findDeviceBySlot(controller, slotId);

				if (device == nullptr or !device->isHub or device->hubInterruptEndpointId != endpointId) {
					++it;
					continue;
				}

				pending.emplace_back(slotId, endpointId);
				it = controller.pendingTransferEvents.erase(it);
			}
		}

		for (const auto &[slotId, endpointId] : pending) {
			XhciUtils::handleHubInterruptTransfer(controller, slotId, endpointId);
		}
	}

	void XhciUtils::pollHubChanges(MappedController &controller) {
		XhciUtils::drainPendingHubInterruptEvents(controller);

		vector<uint8_t> hubSlots;

		for (const auto &device : controller.devices) {
			if (device.isHub and device.configured) {
				hubSlots.push_back(device.slotId);
			}
		}

		for (const uint8_t hubSlotId : hubSlots) {
			auto *hub = XhciUtils::findDeviceBySlot(controller, hubSlotId);

			if (hub == nullptr) {
				continue;
			}

			vector<uint8_t> changedPorts;

			for (uint8_t port = 1; port <= hub->hubPortCount; ++port) {
				uint16_t status = 0;
				uint16_t change = 0;

				if (!XhciUtils::hubPortStatus(controller, *hub, port, status, change)) {
					continue;
				}

				if (change != 0 and ranges::find(changedPorts, port) == changedPorts.end()) {
					changedPorts.push_back(port);
				}
			}

			for (const uint8_t port : changedPorts) {
				XhciUtils::handleHubPortChange(controller, hubSlotId, port);
			}

			hub = XhciUtils::findDeviceBySlot(controller, hubSlotId);

			if (hub != nullptr) {
				XhciUtils::submitHubInterruptTransfer(controller, *hub);
			}
		}
	}

	void XhciUtils::pollRootPortChanges(MappedController &controller) {
		for (uint32_t port = 1; port <= controller.maxPorts; ++port) {
			const uint32_t offset = XHCI_OP_PORT_REGS + ((port - 1) * XHCI_OP_PORT_STRIDE) + XHCI_PORTSC;
			const uint32_t portsc = XhciUtils::mmioRead32(controller.operationalBase, offset);

			if ((portsc & XHCI_PORTSC_CHANGE_BITS) != 0) {
				XhciUtils::handleRootPortChange(controller, port);
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

					XhciUtils::dmaReadFence();

					const uint32_t type = XhciUtils::eventType(event);
					const uint32_t code = XhciUtils::completionCode(event);
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

					XhciUtils::updateEventDequeuePointer(*controller);
				}

				if (drained != 0) {
					XhciUtils::acknowledgeEvents(*controller);
				}
			}

			for (const uint32_t portId : changedPorts) {
				XhciUtils::handleRootPortChange(*controller, portId);
			}

			for (const auto &[slotId, endpointId] : hubTransferCompletions) {
				XhciUtils::handleHubInterruptTransfer(*controller, slotId, endpointId);
			}

			if (drained == 0 and loggedEvents < 32) {
				if (controller->memory.usingMsix) {
					printf("XHCI: MSI-X vector %u irq=%lu cpu=%lu had no completed events.", controller->memory.msixVector, irq.irqNum, irq.cpuId);
				} else if (controller->memory.usingMsi) {
					printf("XHCI: MSI vector %u irq=%lu cpu=%lu had no completed events.", controller->memory.msiVector, irq.irqNum, irq.cpuId);
				} else {
					printf("XHCI: legacy IRQ %u irq=%lu cpu=%lu had no completed events.", controller->memory.legacyIrq, irq.irqNum, irq.cpuId);
				}
				fflush(stdout);

				++loggedEvents;
			}
		}
	}

	auto XhciUtils::startEventIrqHandler(MappedController &controller) -> bool {
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

		return controller != nullptr and XhciUtils::bulkOrInterruptTransfer(*controller, device, endpoint, pagePhysArray, pageCount, length, in, actualLength);
	}

	auto resetBulkOnlyCallback(const void */*ctx*/, XhciDevice &device, const uint8_t interfaceNumber) -> bool {
		if (activeControllers == nullptr or device.controllerId >= activeControllers->size()) {
			return false;
		}

		auto &controller = (*activeControllers)[device.controllerId];
		return XhciUtils::controlTransferNoData(controller, device, 0x21, USB_MASS_STORAGE_REQUEST_BULK_ONLY_RESET, 0, interfaceNumber);
	}

	auto clearEndpointHaltCallback(const void */*ctx*/, XhciDevice &device, UsbEndpoint &endpoint) -> bool {
		if (activeControllers == nullptr or device.controllerId >= activeControllers->size()) {
			return false;
		}

		auto &controller = (*activeControllers)[device.controllerId];
		const bool cleared = XhciUtils::controlTransferNoData(controller, device, 0x02, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, endpoint.address);
		XhciUtils::recoverEndpoint(controller, device, endpoint);
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

	void XhciUtils::startStorageHandlers() {
		pthread_t readThread {};
		pthread_t writeThread {};
		pthread_t flushThread {};

		pthread_create(&readThread, nullptr, storageReadHandler, nullptr);
		pthread_create(&writeThread, nullptr, storageWriteHandler, nullptr);
		pthread_create(&flushThread, nullptr, storageFlushHandler, nullptr);
	}

	auto XhciUtils::setupInterrupts(MappedController &controller) -> bool {
		if (register_horizonos_port(reinterpret_cast<long *>(&controller.memory.eventPort)) != 0 or controller.memory.eventPort == 0) {
			return false;
		}

		controller.memory.msixVector = XhciUtils::msixAllocVector(controller.pci, 0, controller.memory.eventPort);

		if (controller.memory.msixVector != 0) {
			XhciUtils::msixGlobalEnable(controller.pci);

			controller.memory.usingMsix = true;

			printf("XHCI: MSI-X entry 0 vector=%u eventPort=%lu.", controller.memory.msixVector, controller.memory.eventPort);
			fflush(stdout);

			return true;
		}

		printf("XHCI: MSI-X unavailable for %02x:%02x.%x, trying MSI.", controller.pci.bus, controller.pci.device, controller.pci.function);
		fflush(stdout);

		controller.memory.msiVector = XhciUtils::msiAllocVector(controller.pci, controller.memory.eventPort);

		if (controller.memory.msiVector != 0) {
			controller.memory.usingMsi = true;

			printf("XHCI: MSI vector=%u eventPort=%lu.", controller.memory.msiVector, controller.memory.eventPort);
			fflush(stdout);

			return true;
		}

		printf("XHCI: MSI unavailable for %02x:%02x.%x, trying legacy INTx.", controller.pci.bus, controller.pci.device, controller.pci.function);
		fflush(stdout);

			const uint32_t interruptInfo = XhciUtils::pciRead32(controller.pci, PCI_INTERRUPT_LINE);
			const uint8_t interruptPin = static_cast<uint8_t>((interruptInfo >> 8) & 0xFFU);
			const uint8_t interruptLine = static_cast<uint8_t>(interruptInfo & 0xFFU);

		if (interruptPin == 0 or interruptLine == 0 or interruptLine == 0xFF) {
			printf("XHCI: No usable legacy interrupt line for %02x:%02x.%x pin=%u line=%u.",
			       controller.pci.bus,
			       controller.pci.device,
			       controller.pci.function,
			       interruptPin,
			       interruptLine);
			fflush(stdout);

			return false;
		}

		if (install_irq_handler(interruptLine, controller.memory.eventPort) != 0) {
			printf("XHCI: Failed to install legacy IRQ handler line=%u for %02x:%02x.%x.",
			       interruptLine,
			       controller.pci.bus,
			       controller.pci.device,
			       controller.pci.function);
			fflush(stdout);

			return false;
		}

		const uint32_t command = XhciUtils::pciRead32(controller.pci, PCI_COMMAND);
		XhciUtils::pciWrite32(controller.pci, PCI_COMMAND, command & ~PCI_COMMAND_INTERRUPT_DISABLE);

		controller.memory.legacyIrq = interruptLine;
		controller.memory.usingLegacyIrq = true;

		printf("XHCI: Legacy INTx pin=%u irq=%u eventPort=%lu.", interruptPin, interruptLine, controller.memory.eventPort);
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

		XhciUtils::fillName(data.name, sizeof(data.name), data.nameLength, deviceName);

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

		auto XhciUtils::setupControllerMemory(MappedController &controller, const uint32_t maxScratchpads) -> bool {
			if (!XhciUtils::allocatePage(controller.memory.dcbaa, controller.dmaAddressLimit)) {
				return false;
			}

			if (!XhciUtils::setupScratchpads(controller.memory, maxScratchpads, controller.dmaAddressLimit)) {
				return false;
			}

			if (!XhciUtils::setupCommandRing(controller.memory, controller.operationalBase, controller.dmaAddressLimit)) {
				return false;
			}

			if (!XhciUtils::setupEventRing(controller.memory, controller.runtimeBase, controller.dmaAddressLimit)) {
			return false;
		}

		XhciUtils::mmioWrite64(controller.operationalBase, XHCI_OP_DCBAAP, controller.memory.dcbaa.phys);

		return true;
	}

	auto XhciUtils::bringUpController(MappedController &controller, const size_t index) -> bool {
		const uint64_t base = controller.mmioVirt;
		const uint8_t capLength = XhciUtils::mmioRead8(base, XHCI_CAP_CAPLENGTH);
		const uint32_t firstCapDword = XhciUtils::mmioRead32(base, 0x00);
		const auto version = static_cast<uint16_t>(firstCapDword >> 16);
		const uint32_t hcsParams1 = XhciUtils::mmioRead32(base, XHCI_CAP_HCSPARAMS1);
		const uint32_t hcsParams2 = XhciUtils::mmioRead32(base, XHCI_CAP_HCSPARAMS2);
		const uint32_t hccParams1 = XhciUtils::mmioRead32(base, XHCI_CAP_HCCPARAMS1);
		const uint32_t doorbellOffset = XhciUtils::mmioRead32(base, XHCI_CAP_DBOFF) & ~0x3U;
		const uint32_t runtimeOffset = XhciUtils::mmioRead32(base, XHCI_CAP_RTSOFF) & ~0x1FU;

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
		const uint32_t maxScratchpads = XhciUtils::maxScratchpadBuffers(hcsParams2);
			const uint32_t pageSizeMask = XhciUtils::mmioRead32(operationalBase, XHCI_OP_PAGESIZE);

			if ((pageSizeMask & 1U) == 0) {
				printf("XHCI: Controller %zu does not support 4 KiB pages (mask=0x%x).", index, pageSizeMask);
				fflush(stdout);

				return false;
			}

		controller.operationalBase = operationalBase;
		controller.runtimeBase = runtimeBase;
		controller.doorbellBase = doorbellBase;
		controller.maxSlots = maxSlots;
			controller.maxPorts = maxPorts;
			controller.maxInterrupters = maxInterrupters;
			controller.dmaAddressLimit = (hccParams1 & 1U) != 0 ? 0 : (1ULL << 32);
			controller.uses64ByteContexts = (hccParams1 & (1U << 2)) != 0;
			XhciUtils::discoverRootPortProtocols(controller, base, hccParams1);

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

			XhciUtils::takeBiosOwnership(base, hccParams1);

			if (controller.dmaAddressLimit != 0) {
				printf("XHCI: Controller %zu requires DMA addresses below 4 GiB.", index);
				fflush(stdout);
			}

		if (!XhciUtils::haltController(operationalBase)) {
			printf("XHCI: Controller %zu did not halt.", index);
			fflush(stdout);

			return false;
		}

		if (!XhciUtils::resetController(operationalBase)) {
			printf("XHCI: Controller %zu reset timed out.", index);
			fflush(stdout);

			return false;
		}

		if (!XhciUtils::setupControllerMemory(controller, maxScratchpads)) {
			printf("XHCI: Controller %zu failed to allocate controller memory.", index);
			fflush(stdout);

			XhciUtils::releaseControllerMemory(controller.memory);

			return false;
		}

		const uint32_t configSlots = min<uint32_t>(maxSlots, XHCI_MAX_CONFIGURED_SLOTS);

		controller.configuredSlots = configSlots;
		XhciUtils::mmioWrite32(operationalBase, XHCI_OP_CONFIG, configSlots);

		if (!XhciUtils::setupInterrupts(controller)) {
			printf("XHCI: Controller %zu failed to configure interrupts.", index);
			fflush(stdout);

			XhciUtils::releaseControllerMemory(controller.memory);

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

auto XhciService::start() -> int {
	if (register_horizonos_port(reinterpret_cast<long *>(&xhciPort)) != 0 or xhciPort == 0) {
		printf("XHCI: Failed to register port.");
		fflush(stdout);

		return 1;
	}

	printf("XHCI: Successfully registered port!");
	fflush(stdout);

	const GetReplyMsgData pciInfo = XhciUtils::waitForService("PCI");

	pciPort = pciInfo.port;

	printf("XHCI: PCI info: Port: %lu, TID: %u, Version: %u.%u.%u.", pciInfo.port, pciInfo.tid, pciInfo.versionMajor, pciInfo.versionMinor, pciInfo.versionPatch);
	fflush(stdout);

	const GetReplyMsgData storageInfo = XhciUtils::waitForService("StorageManager");

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

	XhciUtils::startStorageHandlers();

	const vector<PciDevice> devices = XhciUtils::findControllers();

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

		if (!XhciUtils::mapBar0(device, controller)) {
			controllers.pop_back();
			continue;
		}

		if (!XhciUtils::bringUpController(controller, controllerIndex)) {
			munmap_extra(reinterpret_cast<void *>(controller.mmioVirt), controller.barSize, false);
			XhciUtils::pciWrite32(device, PCI_COMMAND, controller.originalCommand);

			controllers.pop_back();
			
			continue;
		}
		auto &activeController = controller;

		XhciUtils::setInterrupterEnabled(activeController, false);

		if (!XhciUtils::startController(activeController.operationalBase, false)) {
			printf("XHCI: Controller %zu failed to start.", controllerIndex);
			fflush(stdout);

			continue;
		}

		printf("XHCI: Controller %zu started, configured %u device slot(s).", controllerIndex, activeController.configuredSlots);
		fflush(stdout);

		XhciUtils::powerRootPorts(activeController);

		XhciUtils::postStartProbe(activeController);

		XhciUtils::enumerateRootPorts(activeController, static_cast<uint32_t>(controllerIndex));

		if (!XhciUtils::startEventIrqHandler(activeController)) {
			continue;
		}

		XhciUtils::setInterrupterEnabled(activeController, true);
		XhciUtils::setControllerInterruptsEnabled(activeController.operationalBase, true);

		XhciUtils::logControllerStatus(activeController, "irq-enabled");
	}

	printf("XHCI: %zu controller(s) initialized.", controllers.size());
	fflush(stdout);

	if (controllers.empty()) {
		return 2;
	}

	if (!XhciUtils::registerWithNameRegistry()) {
		printf("XHCI: Failed to register service.");
		fflush(stdout);

		return 1;
	}

	printf("XHCI: Successfully registered service!");
	fflush(stdout);

	for (;;) {
		for (auto &controller : controllers) {
			XhciUtils::pollRootPortChanges(controller);
			XhciUtils::pollHubChanges(controller);
		}

		usleep(1000000);
	}
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	XhciService service;

	return service.start();
}
