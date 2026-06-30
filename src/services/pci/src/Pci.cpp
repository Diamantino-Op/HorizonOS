#include "Pci.hpp"
#include "Msi.hpp"

#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"

#include <sys/io.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>

using namespace std;

// ─── Global ECAM table ───────────────────────────────────────────────────────
vector<McfgSegment> g_ecamSegments;
mutex               g_ecamMutex;

// ─── ECAM helpers ────────────────────────────────────────────────────────────

auto addEcamSegment(const uint64_t physBase, const uint16_t seg, const uint8_t startBus, const uint8_t endBus) -> bool {
    // Each bus needs 256 devices × 8 functions × 4 KiB = 1 MiB.
    const auto   busCount = static_cast<size_t>(endBus - startBus + 1);
    const size_t mapSize  = busCount * 256U * 8U * 4096U;

    uint64_t virt = 0;
    if (mmap_phys(physBase, mapSize, &virt, false, MMapCacheMode::MAP_CACHE_UC) != 0) {
        printf("PCI: ECAM mmap_phys failed for segment %u (base=%lx)\n", seg, physBase);

        return false;
    }

    const scoped_lock lock(g_ecamMutex);
    g_ecamSegments.push_back({
        physBase,
        seg,
        startBus,
        endBus,
        reinterpret_cast<void *>(virt),
        mapSize
    });

    printf("PCI: ECAM segment %u mapped: phys=%lx virt=%lx buses=%u-%u", seg, physBase, virt, startBus, endBus);
	fflush(stdout);

    return true;
}

// Returns a pointer to the 4 KiB config space for a BDF, or nullptr.
auto ecamDeviceBase(const uint8_t bus, const uint8_t dev, const uint8_t func) -> void * {
    const scoped_lock lock(g_ecamMutex);

    for (const auto &seg : g_ecamSegments) {
        if (bus < seg.startBus || bus > seg.endBus) {
            continue;
        }

        if (seg.mappedBase == nullptr) {
            continue;
        }

        const size_t offset = (static_cast<size_t>(bus  - seg.startBus) << 20)
                            | (static_cast<size_t>(dev)                 << 15)
                            | (static_cast<size_t>(func)                << 12);

        return static_cast<uint8_t *>(seg.mappedBase) + offset;
    }

    return nullptr;
}

// ─── Legacy port-I/O helpers ─────────────────────────────────────────────────

static auto legacyRead32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint8_t offset) -> uint32_t {
    const uint32_t address = (1U << 31)
                           | (static_cast<uint32_t>(bus)  << 16)
                           | (static_cast<uint32_t>(dev)  << 11)
                           | (static_cast<uint32_t>(func) <<  8)
                           | (offset & 0xFCU);

    outl(address, PCI_CONFIG_ADDRESS);

    return inl(PCI_CONFIG_DATA);
}

static void legacyWrite32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint8_t offset, const uint32_t value) {
    const uint32_t address = (1U << 31)
                           | (static_cast<uint32_t>(bus)  << 16)
                           | (static_cast<uint32_t>(dev)  << 11)
                           | (static_cast<uint32_t>(func) <<  8)
                           | (offset & 0xFCU);

    outl(address, PCI_CONFIG_ADDRESS);
    outl(value, PCI_CONFIG_DATA);
}

// ─── Unified config-space accessors ──────────────────────────────────────────
// Offset is uint16_t so PCIe extended config space (0x100–0xFFF) is reachable.

auto pciConfigRead32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset) -> uint32_t {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        uint32_t val;

        memcpy(&val, static_cast<uint8_t *>(base) + offset, 4);

        return val;
    }

    // Legacy fallback — only the low 8 bits of offset are usable.
    return legacyRead32(bus, dev, func, static_cast<uint8_t>(offset));
}

auto pciConfigRead16(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset) -> uint16_t {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        uint16_t val;

        memcpy(&val, static_cast<uint8_t *>(base) + offset, 2);

        return val;
    }

    const uint32_t dword = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU));

    return static_cast<uint16_t>((dword >> ((offset & 2U) * 8U)) & 0xFFFFU);
}

auto pciConfigRead8(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset) -> uint8_t {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        return *(static_cast<uint8_t *>(base) + offset);
    }

    const uint32_t dword = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU));

    return static_cast<uint8_t>((dword >> ((offset & 3U) * 8U)) & 0xFFU);
}

void pciConfigWrite32(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset, const uint32_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        memcpy(static_cast<uint8_t *>(base) + offset, &value, 4);

        return;
    }

    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset), value);
}

void pciConfigWrite16(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset, const uint16_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        memcpy(static_cast<uint8_t *>(base) + offset, &value, 2);

        return;
    }

    // Legacy: read-modify-write on the containing dword.
    uint32_t cur = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU));

    const uint32_t shift = (offset & 2U) * 8U;

    cur &= ~(0xFFFFU << shift);
    cur |= static_cast<uint32_t>(value) << shift;

    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU), cur);
}

void pciConfigWrite8(const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset, const uint8_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base != nullptr) {
        *(static_cast<uint8_t *>(base) + offset) = value;

        return;
    }

    uint32_t cur = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU));

    const uint32_t shift = (offset & 3U) * 8U;

    cur &= ~(0xFFU << shift);
    cur |= static_cast<uint32_t>(value) << shift;

    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCU), cur);
}

// ─── Bus enumeration ─────────────────────────────────────────────────────────

static void checkFunction(const uint8_t bus, const uint8_t dev, const uint8_t func, vector<PciDevice> &devices) {
    const uint32_t id = pciConfigRead32(bus, dev, func, 0x00);

    if ((id & 0xFFFF) == 0xFFFF) {
        return;
    }

    PciDevice d{};

    d.bus      = bus;
    d.device   = dev;
    d.function = func;
    d.vendorId = static_cast<uint16_t>(id & 0xFFFFU);
    d.deviceId = static_cast<uint16_t>(id >> 16U);
    d.isPcie   = (ecamDeviceBase(bus, dev, func) != nullptr);

    const uint32_t classRev = pciConfigRead32(bus, dev, func, 0x08);

    d.classCode = static_cast<uint8_t>(classRev >> 24U);
    d.subclass  = static_cast<uint8_t>((classRev >> 16U) & 0xFFU);
    d.progIf    = static_cast<uint8_t>((classRev >>  8U) & 0xFFU);

    const uint32_t hdrType = pciConfigRead32(bus, dev, func, 0x0C);

    d.headerType = static_cast<uint8_t>((hdrType >> 16U) & 0xFFU);

    devices.push_back(d);
}

static void checkDevice(const uint8_t bus, const uint8_t dev, vector<PciDevice> &devices, vector<bool> &visitedBuses) {
	const uint32_t id = pciConfigRead32(bus, dev, 0, 0x00);

	if ((id & 0xFFFF) == 0xFFFF) {
		return;
	}

	checkFunction(bus, dev, 0, devices);

	// Read headerType directly - before anymore checkFunction calls push
	// onto `devices` and invalidate any .back() reference.
	const uint8_t headerType = pciConfigRead8(bus, dev, 0, 0x0E);

	if (static_cast<bool>(headerType & 0x80U)) {
		for (uint8_t func = 1; func < 8; ++func) {
			const uint32_t fid = pciConfigRead32(bus, dev, func, 0x00);

			if ((fid & 0xFFFF) != 0xFFFF) {
				checkFunction(bus, dev, func, devices);
			}
		}
	}

	const uint8_t classCode = pciConfigRead8(bus, dev, 0, 0x0B);
	const uint8_t subclass  = pciConfigRead8(bus, dev, 0, 0x0A);
	const uint8_t progIf    = pciConfigRead8(bus, dev, 0, 0x09);

	if (classCode == 0x06 and subclass == 0x04 and progIf != 0x01) {
		const uint8_t secondaryBus = pciConfigRead8(bus, dev, 0, 0x19);

		if (secondaryBus != 0 and secondaryBus != bus and !visitedBuses[secondaryBus]) {
			visitedBuses[secondaryBus] = true;

			for (uint8_t subDev = 0; subDev < 32; ++subDev) {
				checkDevice(secondaryBus, subDev, devices, visitedBuses);
			}
		}
	}
}

void enumeratePci(vector<PciDevice> &devices) {
    // Acquire legacy I/O permissions in case ECAM is absent for some buses.
    ioperm(PCI_CONFIG_ADDRESS, 8, 1);

	vector visitedBuses(256, false);

	for (const auto &seg : g_ecamSegments) {
		printf("PCI: Enumerating root complex seg=%u bus %02x-%02x", seg.segmentGroup, seg.startBus, seg.endBus);
		fflush(stdout);

		if (!visitedBuses[seg.startBus]) {
			visitedBuses[seg.startBus] = true;

			for (uint8_t dev = 0; dev < 32; ++dev) {
				checkDevice(seg.startBus, dev, devices, visitedBuses);
			}
		}
	}

    ioperm(PCI_CONFIG_ADDRESS, 8, 0);
}

auto getPciBridgeType(const uint8_t classCode, const uint8_t subclass) -> PciBridgeType {
	if (classCode != 0x06) {
		return PciBridgeType::None;
	}

	switch (subclass) {
		case 0x00:
			return PciBridgeType::HostBridge;

		case 0x01:
			return PciBridgeType::IsaBridge;

		case 0x04:
			return PciBridgeType::PciToPciBridge;

		default:
			return PciBridgeType::OtherBridge;
	}
}

auto isPciBridge(const uint8_t classCode, const uint8_t subclass) -> bool {
	(void) subclass;

	return classCode == 0x06;
}

// ─── Message loop ────────────────────────────────────────────────────────────

auto handleSearchDevice(void *devicesArr) -> void * {
	const auto *devices = static_cast<vector<PciDevice> *>(devicesArr);

	printf("PCI: Search Device message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	auto sendData = PciDevice();

	sendMsg.type = PCI_SEARCH_DEVICE_REPLY_MSG_TYPE;
	sendMsg.buffer = &sendData;
	sendMsg.length = sizeof(PciDevice);

	// Send Start

	auto sendStartMsg = hos_msg();

	uint64_t sendStartAmount = 0;

	sendStartMsg.type = PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE;
	sendStartMsg.buffer = &sendStartAmount;
	sendStartMsg.length = sizeof(uint64_t);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciSearchDeviceMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciSearchDeviceMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_SEARCH_DEVICE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		sendMsg.port = recvMsg.src_port;
		sendStartMsg.port = recvMsg.src_port;
		sendStartAmount = 0;

		vector<PciDevice> matchedDevices {};

		for (const auto device : *devices) {
			if (device.classCode == recvData.pciClass && device.subclass == recvData.pciSubclass && device.progIf == recvData.pciProg) {
				matchedDevices.push_back(device);

				sendStartAmount++;
			}
		}

		send_horizonos_message(pciPort, recvMsg.src_port, &sendStartMsg);

		for (const auto &d : matchedDevices) {
			sendData = d;

			send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
		}
	}
}

auto handlePciRead(void *arg) -> void * {
	(void) arg;

	printf("PCI: Read message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	auto sendData = PciReadReplyMsgData();

	sendMsg.type = PCI_READ_REPLY_MSG_TYPE;
	sendMsg.buffer = &sendData;
	sendMsg.length = sizeof(PciReadReplyMsgData);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciReadMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciReadMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_READ_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			printf("PCI: Read message failed: %d!", result);
			fflush(stdout);

			continue;
		}

		sendMsg.port = recvMsg.src_port;

		sendData.data = 0;

		if (recvData.width == 8) {
			sendData.data = pciConfigRead8(recvData.bus, recvData.dev, recvData.func, recvData.offset);
		} else if (recvData.width == 16) {
			sendData.data = pciConfigRead16(recvData.bus, recvData.dev, recvData.func, recvData.offset);
		} else if (recvData.width == 32) {
			sendData.data = pciConfigRead32(recvData.bus, recvData.dev, recvData.func, recvData.offset);
		}

		const int sendRes = send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);

		if (sendRes != 0) {
			printf("PCI: Read message send failed: %d!", sendRes);
			fflush(stdout);
		}
	}
}

auto handlePciWrite(void *arg) -> void * {
	(void) arg;

	printf("PCI: Write message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	sendMsg.type = PCI_WRITE_REPLY_MSG_TYPE;
	sendMsg.length = 0;

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciWriteMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciWriteMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_WRITE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		if (recvData.width == 8) {
			pciConfigWrite8(recvData.bus, recvData.dev, recvData.func, recvData.offset, static_cast<uint8_t>(recvData.data));
		} else if (recvData.width == 16) {
			pciConfigWrite16(recvData.bus, recvData.dev, recvData.func, recvData.offset, static_cast<uint16_t>(recvData.data));
		} else if (recvData.width == 32) {
			pciConfigWrite32(recvData.bus, recvData.dev, recvData.func, recvData.offset, recvData.data);
		}

		sendMsg.port = recvMsg.src_port;

		send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
	}
}

auto handleMsiAlloc(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msi Alloc message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	auto sendData = PciMsiAllocReplyMsgData();

	sendMsg.type = PCI_MSI_ALLOC_REPLY_MSG_TYPE;
	sendMsg.buffer = &sendData;
	sendMsg.length = sizeof(PciMsiAllocReplyMsgData);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsiAllocMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsiAllocMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSI_ALLOC_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		sendMsg.port = recvMsg.src_port;

		sendData.vec = msiEnable(recvData.bus, recvData.dev, recvData.func, recvData.port, recvData.lapicId);

		send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
	}
}

auto handleMsiFree(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msi Free message loop started!");
	fflush(stdout);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsiFreeMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsiFreeMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSI_FREE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		msiDisable(recvData.bus, recvData.dev, recvData.func);
	}
}

auto handleMsixAlloc(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msix Alloc message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	auto sendData = PciMsixAllocReplyMsgData();

	sendMsg.type = PCI_MSIX_ALLOC_REPLY_MSG_TYPE;
	sendMsg.buffer = &sendData;
	sendMsg.length = sizeof(PciMsixAllocReplyMsgData);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsixAllocMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsixAllocMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSIX_ALLOC_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}


		sendMsg.port = recvMsg.src_port;

		// Ensure global MSI-X enable is set after all desired entries are
		// programmed.  The caller should send msix_global_enable when done.
		sendData.vec = msixEnableEntry(recvData.bus, recvData.dev, recvData.func, recvData.idx, recvData.port, recvData.lapicId);

		send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
	}
}

auto handleMsixFree(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msix Free message loop started!");
	fflush(stdout);

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsixFreeMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsixFreeMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSIX_FREE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		msixDisableEntry(recvData.bus, recvData.dev, recvData.func, recvData.idx, recvData.vec);
	}
}

auto handleMsixGlobalEnable(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msix Global Enable message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	sendMsg.type = PCI_MSIX_GLOBAL_ENABLE_REPLY_MSG_TYPE;
	sendMsg.length = 0;

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsixGlobalEnableMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsixGlobalEnableMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		msixGlobalEnable(recvData.bus, recvData.dev, recvData.func);

		sendMsg.port = recvMsg.src_port;

		send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
	}
}

auto handleMsixGlobalDisable(void *arg) -> void * {
	(void) arg;

	printf("PCI: Msix Global Disable message loop started!");
	fflush(stdout);

	// Send

	auto sendMsg = hos_msg();

	sendMsg.type = PCI_MSIX_GLOBAL_DISABLE_REPLY_MSG_TYPE;
	sendMsg.length = 0;

	// Recv

	auto recvMsg = hos_msg();

	auto recvData = PciMsixGlobalDisableMsgData();

	recvMsg.buffer = &recvData;
	recvMsg.length = sizeof(PciMsixGlobalDisableMsgData);

	auto filterOptions = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_MSIX_GLOBAL_DISABLE_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, &recvMsg, &filterOptions);

		if (result != 0) {
			continue;
		}

		msixGlobalDisable(recvData.bus, recvData.dev, recvData.func);

		sendMsg.port = recvMsg.src_port;

		send_horizonos_message(pciPort, recvMsg.src_port, &sendMsg);
	}
}
