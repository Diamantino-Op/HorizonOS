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
#include <array>
#include <thread>
#include <unistd.h>
#include <mutex>

using namespace std;

extern uint64_t pciPort;
extern uint64_t uacpiPort;

// ─── Global ECAM table ───────────────────────────────────────────────────────
vector<McfgSegment> g_ecamSegments;
mutex               g_ecamMutex;

// ─── ECAM helpers ────────────────────────────────────────────────────────────

bool addEcamSegment(uint64_t physBase, uint16_t seg, uint8_t startBus, uint8_t endBus) {
    // Each bus needs 256 devices × 8 functions × 4 KiB = 1 MiB.
    const size_t busCount = static_cast<size_t>(endBus - startBus + 1);
    const size_t mapSize  = busCount * 256u * 8u * 4096u;

    uint64_t virt = 0;
    if (mmap_phys(physBase, mapSize, &virt) != 0) {
        printf("PCI: ECAM mmap_phys failed for segment %u (base=%lx)\n", seg, physBase);

        return false;
    }

    scoped_lock lock(g_ecamMutex);
    g_ecamSegments.push_back({
        physBase,
        seg,
        startBus,
        endBus,
        reinterpret_cast<void *>(virt),
        mapSize
    });

    printf("PCI: ECAM segment %u mapped: phys=%lx virt=%lx buses=%u-%u\n", seg, physBase, virt, startBus, endBus);

    return true;
}

// Returns a pointer to the 4 KiB config space for a BDF, or nullptr.
void *ecamDeviceBase(uint8_t bus, uint8_t dev, uint8_t func) {
    scoped_lock lock(g_ecamMutex);

    for (const auto &seg : g_ecamSegments) {
        if (bus < seg.startBus || bus > seg.endBus) {
            continue;
        }

        if (!seg.mappedBase) {
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

static uint32_t legacyRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    const uint32_t address = (1u << 31)
                           | (static_cast<uint32_t>(bus)  << 16)
                           | (static_cast<uint32_t>(dev)  << 11)
                           | (static_cast<uint32_t>(func) <<  8)
                           | (offset & 0xFCu);

    outl(address, PCI_CONFIG_ADDRESS);
    return inl(PCI_CONFIG_DATA);
}

static void legacyWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    const uint32_t address = (1u << 31)
                           | (static_cast<uint32_t>(bus)  << 16)
                           | (static_cast<uint32_t>(dev)  << 11)
                           | (static_cast<uint32_t>(func) <<  8)
                           | (offset & 0xFCu);

    outl(address, PCI_CONFIG_ADDRESS);
    outl(value,   PCI_CONFIG_DATA);
}

// ─── Unified config-space accessors ──────────────────────────────────────────
// Offset is uint16_t so PCIe extended config space (0x100–0xFFF) is reachable.

uint32_t pciConfigRead32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        uint32_t val;

        memcpy(&val, static_cast<uint8_t *>(base) + offset, 4);

        return val;
    }

    // Legacy fallback — only the low 8 bits of offset are usable.
    return legacyRead32(bus, dev, func, static_cast<uint8_t>(offset));
}

uint16_t pciConfigRead16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        uint16_t val;

        memcpy(&val, static_cast<uint8_t *>(base) + offset, 2);

        return val;
    }

    const uint32_t dword = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu));

    return static_cast<uint16_t>((dword >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pciConfigRead8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        return *(static_cast<uint8_t *>(base) + offset);
    }

    const uint32_t dword = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu));

    return static_cast<uint8_t>((dword >> ((offset & 3u) * 8u)) & 0xFFu);
}

void pciConfigWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        memcpy(static_cast<uint8_t *>(base) + offset, &value, 4);

        return;
    }

    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset), value);
}

void pciConfigWrite16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        memcpy(static_cast<uint8_t *>(base) + offset, &value, 2);

        return;
    }

    // Legacy: read-modify-write on the containing dword.
    uint32_t cur = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu));

    const uint32_t shift = (offset & 2u) * 8u;

    cur &= ~(0xFFFFu << shift);
    cur |= static_cast<uint32_t>(value) << shift;

    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu), cur);
}

void pciConfigWrite8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t value) {
    void *base = ecamDeviceBase(bus, dev, func);

    if (base) {
        *(static_cast<uint8_t *>(base) + offset) = value;
        return;
    }

    uint32_t cur = legacyRead32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu));
    const uint32_t shift = (offset & 3u) * 8u;
    cur &= ~(0xFFu << shift);
    cur |= static_cast<uint32_t>(value) << shift;
    legacyWrite32(bus, dev, func, static_cast<uint8_t>(offset & 0xFCu), cur);
}

// ─── Bus enumeration ─────────────────────────────────────────────────────────

static void checkFunction(uint8_t bus, uint8_t dev, uint8_t func, vector<PciDevice> &devices) {
    const uint32_t id = pciConfigRead32(bus, dev, func, 0x00);

    if ((id & 0xFFFF) == 0xFFFF) {
        return;
    }

    PciDevice d{};
    d.bus      = bus;
    d.device   = dev;
    d.function = func;
    d.vendorId = static_cast<uint16_t>(id & 0xFFFFu);
    d.deviceId = static_cast<uint16_t>(id >> 16u);
    d.isPcie   = (ecamDeviceBase(bus, dev, func) != nullptr);

    const uint32_t classRev = pciConfigRead32(bus, dev, func, 0x08);
    d.classCode = static_cast<uint8_t>(classRev >> 24u);
    d.subclass  = static_cast<uint8_t>((classRev >> 16u) & 0xFFu);
    d.progIf    = static_cast<uint8_t>((classRev >>  8u) & 0xFFu);

    const uint32_t hdrType = pciConfigRead32(bus, dev, func, 0x0C);
    d.headerType = static_cast<uint8_t>((hdrType >> 16u) & 0xFFu);

    devices.push_back(d);
}

static void checkDevice(uint8_t bus, uint8_t dev,vector<PciDevice> &devices,vector<bool> &visitedBuses) {
	const uint32_t id = pciConfigRead32(bus, dev, 0, 0x00);

	if ((id & 0xFFFF) == 0xFFFF) {
		return;
	}

	checkFunction(bus, dev, 0, devices);

	// Read headerType directly — before any more checkFunction calls push
	// onto `devices` and invalidate any .back() reference.
	const uint8_t headerType = pciConfigRead8(bus, dev, 0, 0x0E);

	if (headerType & 0x80u) {
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

	if (classCode == 0x06 && subclass == 0x04 && progIf != 0x01) {
		const uint8_t secondaryBus = pciConfigRead8(bus, dev, 0, 0x19);

		if (secondaryBus != 0 && secondaryBus != bus && !visitedBuses[secondaryBus]) {
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
		printf("PCI: Enumerating root complex seg=%u bus %02x-%02x\n", seg.segmentGroup, seg.startBus, seg.endBus);

		if (!visitedBuses[seg.startBus]) {
			visitedBuses[seg.startBus] = true;

			for (uint8_t dev = 0; dev < 32; ++dev) {
				checkDevice(seg.startBus, dev, devices, visitedBuses);
			}
		}
	}

    ioperm(PCI_CONFIG_ADDRESS, 8, 0);
}

PciBridgeType getPciBridgeType(uint8_t classCode, uint8_t subclass) {
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

bool isPciBridge(uint8_t classCode, uint8_t subclass) {
	return classCode == 0x06;
}

// ─── Message loop ────────────────────────────────────────────────────────────

void *handlePciRead(void *arg) {
	(void)arg;

	printf("PCI: Read message loop started!\n");

	// Send

	auto *sendMsg = new hos_msg();

	auto *sendData = new PciReadReplyMsgData();

	sendMsg->type = PCI_READ_REPLY_MSG_TYPE;
	sendMsg->buffer = sendData;
	sendMsg->length = sizeof(PciReadReplyMsgData);

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciReadMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciReadMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_READ_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}


		sendMsg->port = recvMsg->src_port;

		sendData->data = 0;

		if (recvData->width == 8) {
			sendData->data = pciConfigRead8(recvData->bus, recvData->dev, recvData->func, recvData->offset);
		} else if (recvData->width == 16) {
			sendData->data = pciConfigRead16(recvData->bus, recvData->dev, recvData->func, recvData->offset);
		} else if (recvData->width == 32) {
			sendData->data = pciConfigRead32(recvData->bus, recvData->dev, recvData->func, recvData->offset);
		}

		send_horizonos_message(pciPort, recvMsg->src_port, sendMsg);
	}
}

void *handlePciWrite(void *arg) {
	(void)arg;

	printf("PCI: Write message loop started!\n");

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciWriteMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciWriteMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_WRITE_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}

		if (recvData->width == 8) {
			pciConfigWrite8 (recvData->bus, recvData->dev, recvData->func, recvData->offset, static_cast<uint8_t>(recvData->data));
		} else if (recvData->width == 16) {
			pciConfigWrite16(recvData->bus, recvData->dev, recvData->func, recvData->offset, static_cast<uint16_t>(recvData->data));
		} else if (recvData->width == 32) {
			pciConfigWrite32(recvData->bus, recvData->dev, recvData->func, recvData->offset, recvData->data);
		}
	}
}

void *handleMsiAlloc(void *arg) {
	(void)arg;

	printf("PCI: Msi Alloc message loop started!\n");

	// Send

	auto *sendMsg = new hos_msg();

	auto *sendData = new PciMsiAllocReplyMsgData();

	sendMsg->type = PCI_MSI_ALLOC_REPLY_MSG_TYPE;
	sendMsg->buffer = sendData;
	sendMsg->length = sizeof(PciMsiAllocReplyMsgData);

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsiAllocMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsiAllocMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSI_ALLOC_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}


		sendMsg->port = recvMsg->src_port;

		sendData->vec = msiEnable(recvData->bus, recvData->dev, recvData->func, recvData->port);

		send_horizonos_message(pciPort, recvMsg->src_port, sendMsg);
	}
}

void *handleMsiFree(void *arg) {
	(void)arg;

	printf("PCI: Msi Free message loop started!\n");

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsiFreeMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsiFreeMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSI_FREE_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}

		msiDisable(recvData->bus, recvData->dev, recvData->func);
	}
}

void *handleMsixAlloc(void *arg) {
	(void)arg;

	printf("PCI: Msix Alloc message loop started!\n");

	// Send

	auto *sendMsg = new hos_msg();

	auto *sendData = new PciMsixAllocReplyMsgData();

	sendMsg->type = PCI_MSIX_ALLOC_REPLY_MSG_TYPE;
	sendMsg->buffer = sendData;
	sendMsg->length = sizeof(PciMsixAllocReplyMsgData);

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsixAllocMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsixAllocMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSIX_ALLOC_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}


		sendMsg->port = recvMsg->src_port;

		// Ensure global MSI-X enable is set after all desired entries are
		// programmed.  The caller should send msix_global_enable when done.
		sendData->vec = msixEnableEntry(recvData->bus, recvData->dev, recvData->func, recvData->idx, recvData->port);

		send_horizonos_message(pciPort, recvMsg->src_port, sendMsg);
	}
}

void *handleMsixFree(void *arg) {
	(void)arg;

	printf("PCI: Msix Free message loop started!\n");

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsixFreeMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsixFreeMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSIX_FREE_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}

		msixDisableEntry(recvData->bus, recvData->dev, recvData->func, recvData->idx, recvData->vec);
	}
}

void *handleMsixGlobalEnable(void *arg) {
	(void)arg;

	printf("PCI: Msix Global Enable message loop started!\n");

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsixGlobalEnableMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsixGlobalEnableMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}

		msixGlobalEnable(recvData->bus, recvData->dev, recvData->func);
	}
}

void *handleMsixGlobalDisable(void *arg) {
	(void)arg;

	printf("PCI: Msix Global Disable message loop started!\n");

	// Recv

	auto *recvMsg = new hos_msg();

	auto *recvData = new PciMsixGlobalDisableMsgData();

	recvMsg->buffer = recvData;
	recvMsg->length = sizeof(PciMsixGlobalDisableMsgData);

	auto *filterOptions = new filter_options();

	filterOptions->whiteListTypes = new uint64_t[1]{ PCI_MSIX_GLOBAL_DISABLE_MSG_TYPE };
	filterOptions->whiteListCount = 1;

	for (;;) {
		const int result = receive_horizonos_message(pciPort, recvMsg, filterOptions);

		if (result != 0) {
			continue;
		}

		msixGlobalDisable(recvData->bus, recvData->dev, recvData->func);
	}
}