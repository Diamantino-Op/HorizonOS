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
        printf("PCI: ECAM mmap_phys failed for segment %u (base=%llx)\n",
               seg, static_cast<unsigned long long>(physBase));
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

    printf("PCI: ECAM segment %u mapped: phys=%llx virt=%llx buses=%u-%u\n",
           seg,
           static_cast<unsigned long long>(physBase),
           static_cast<unsigned long long>(virt),
           startBus, endBus);

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
        return *static_cast<uint8_t *>(static_cast<uint8_t *>(base) + offset);
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
        *static_cast<uint8_t *>(static_cast<uint8_t *>(base) + offset) = value;
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

// ─── Message helpers ──────────────────────────────────────────────────────────

static void sendReply(uint64_t srcPort, const string &payload) {
    auto *reply = new hos_msg();

    reply->port   = static_cast<int>(srcPort);
    reply->buffer = const_cast<void *>(static_cast<const void *>(payload.data()));
    reply->length = payload.size();

    send_horizonos_message(PCI_PORT, static_cast<int>(srcPort), reply);

    delete reply;
}

// ─── Message loop ────────────────────────────────────────────────────────────

void *pciMessageLoop(void *arg) {
    (void)arg;

    // Keep legacy I/O mapped permanently for fallback reads/writes.
    if (ioperm(PCI_CONFIG_ADDRESS, 8, 1) != 0) {
        printf("PCI: Failed to acquire I/O permissions in message loop\n");

        return nullptr;
    }

    printf("PCI: Message loop started\n");

    for (;;) {
        array<char, 4096> buf{};
        auto *msg = new hos_msg();

        msg->buffer = buf.data();
        msg->length = buf.size();

        const int err = receive_horizonos_message(PCI_PORT, msg);

        if (err != 0) {
            delete msg;

            continue;
        }

        if (msg->ret_length <= 0 || static_cast<size_t>(msg->ret_length) > buf.size()) {
            delete msg;

            continue;
        }

        const string message(buf.data(), static_cast<size_t>(msg->ret_length));

        // Split on ';'
        vector<string> parts;
        size_t start = 0;
        while (start <= message.size()) {
            const size_t sep = message.find(';', start);
            if (sep == string::npos) {
                parts.emplace_back(message.substr(start));
                break;
            }
            parts.emplace_back(message.substr(start, sep - start));
            start = sep + 1;
        }

        if (parts.empty()) { delete msg; continue; }

        const string &cmd = parts[0];

        // ── mcfg_segment ── sent by uACPI once per MCFG entry ────────────────
        // Format: "mcfg_segment;<physBase>;<seg>;<startBus>;<endBus>"
        if (cmd == "mcfg_segment") {
            if (parts.size() < 5) { delete msg; continue; }

            const uint64_t physBase = stoull(parts[1]);
            const uint16_t seg      = static_cast<uint16_t>(stoul(parts[2]));
            const uint8_t  startBus = static_cast<uint8_t>(stoul(parts[3]));
            const uint8_t  endBus   = static_cast<uint8_t>(stoul(parts[4]));

            const bool ok = addEcamSegment(physBase, seg, startBus, endBus);

            sendReply(msg->src_port, ok ? "1" : "0");
        }

        // ── pci_read ── uACPI or any caller wants a config read ───────────────
        // Format: "pci_read;<bus>;<dev>;<func>;<offset>;<width>"
        else if (cmd == "pci_read") {
            if (parts.size() < 6) {
	            sendReply(msg->src_port, "error"); delete msg; continue;
            }

            const uint8_t  bus    = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t  dev    = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t  func   = static_cast<uint8_t>(stoul(parts[3]));
            const uint16_t offset = static_cast<uint16_t>(stoul(parts[4]));
            const int      width  = stoi(parts[5]);

            uint32_t value = 0;

            if (width == 8) {
                value = pciConfigRead8 (bus, dev, func, offset);
            } else if (width == 16) {
                value = pciConfigRead16(bus, dev, func, offset);
            } else if (width == 32) {
                value = pciConfigRead32(bus, dev, func, offset);
            } else {
                sendReply(msg->src_port, "error");

                delete msg;

                continue;
            }

            sendReply(msg->src_port, to_string(value));
        }

        // ── pci_write ── uACPI or any caller wants a config write ─────────────
        // Format: "pci_write;<bus>;<dev>;<func>;<offset>;<width>;<value>"
        else if (cmd == "pci_write") {
            if (parts.size() < 7) {
	            delete msg;

            	continue;
            }

            const uint8_t  bus    = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t  dev    = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t  func   = static_cast<uint8_t>(stoul(parts[3]));
            const uint16_t offset = static_cast<uint16_t>(stoul(parts[4]));
            const int      width  = stoi(parts[5]);
            const uint32_t value  = static_cast<uint32_t>(stoul(parts[6]));

            if (width == 8) {
                pciConfigWrite8 (bus, dev, func, offset, static_cast<uint8_t>(value));
            } else if (width == 16) {
                pciConfigWrite16(bus, dev, func, offset, static_cast<uint16_t>(value));
            } else if (width == 32) {
                pciConfigWrite32(bus, dev, func, offset, value);
            }
            // writes are fire-and-forget; no reply needed
        }

        // ── enumerate ── returns all detected devices ─────────────────────────
        // Format: "enumerate"
        // Reply:  "<vendor>:<device>:<bus>:<dev>:<func>:<class>:<sub>:<pcie>\n" per device
        else if (cmd == "enumerate") {
            vector<PciDevice> devices;
            enumeratePci(devices);

            string response;
            for (const auto &d : devices) {
                response += to_string(d.vendorId)  + ":"
                          + to_string(d.deviceId)  + ":"
                          + to_string(d.bus)        + ":"
                          + to_string(d.device)     + ":"
                          + to_string(d.function)   + ":"
                          + to_string(d.classCode)  + ":"
                          + to_string(d.subclass)   + ":"
                          + (d.isPcie ? "1" : "0")  + "\n";
            }

            sendReply(msg->src_port, response.empty() ? "none" : response);
        }

    	// ── msi_alloc ── driver requests MSI on a device ─────────────────────
        // Format:  "msi_alloc;<bus>;<dev>;<func>;<notify_port>"
        // Reply:   "<vector>" or "0" on failure
        else if (cmd == "msi_alloc") {
            if (parts.size() < 5) { sendReply(msg->src_port, "0"); delete msg; continue; }

            const uint8_t bus  = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t dev  = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t func = static_cast<uint8_t>(stoul(parts[3]));
            const int     port = stoi(parts[4]);

            const uint8_t vec = msiEnable(bus, dev, func, port);
            sendReply(msg->src_port, to_string(vec));
        }

        // ── msi_free ── driver releases MSI on a device ──────────────────────
        // Format: "msi_free;<bus>;<dev>;<func>"
        else if (cmd == "msi_free") {
            if (parts.size() < 4) { delete msg; continue; }

            const uint8_t bus  = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t dev  = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t func = static_cast<uint8_t>(stoul(parts[3]));

            msiDisable(bus, dev, func);
        }

        // ── msix_alloc ── driver requests one MSI-X table entry ──────────────
        // Format:  "msix_alloc;<bus>;<dev>;<func>;<table_index>;<notify_port>"
        // Reply:   "<vector>" or "0" on failure
        else if (cmd == "msix_alloc") {
            if (parts.size() < 6) { sendReply(msg->src_port, "0"); delete msg; continue; }

            const uint8_t  bus   = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t  dev   = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t  func  = static_cast<uint8_t>(stoul(parts[3]));
            const uint16_t idx   = static_cast<uint16_t>(stoul(parts[4]));
            const int      port  = stoi(parts[5]);

            // Ensure global MSI-X enable is set after all desired entries are
            // programmed.  The caller should send msix_global_enable when done.
            const uint8_t vec = msixEnableEntry(bus, dev, func, idx, port);
            sendReply(msg->src_port, to_string(vec));
        }

        // ── msix_free ── driver releases one MSI-X entry ─────────────────────
        // Format: "msix_free;<bus>;<dev>;<func>;<table_index>;<vector>"
        else if (cmd == "msix_free") {
            if (parts.size() < 6) { delete msg; continue; }

            const uint8_t  bus    = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t  dev    = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t  func   = static_cast<uint8_t>(stoul(parts[3]));
            const uint16_t idx    = static_cast<uint16_t>(stoul(parts[4]));
            const uint8_t  vector = static_cast<uint8_t>(stoul(parts[5]));

            msixDisableEntry(bus, dev, func, idx, vector);
        }

        // ── msix_global_enable ── enable the MSI-X capability on a device ────
        // Format: "msix_global_enable;<bus>;<dev>;<func>"
        else if (cmd == "msix_global_enable") {
            if (parts.size() < 4) { delete msg; continue; }

            const uint8_t bus  = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t dev  = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t func = static_cast<uint8_t>(stoul(parts[3]));

            msixGlobalEnable(bus, dev, func);
        }

        // ── msix_global_disable ──────────────────────────────────────────────
        // Format: "msix_global_disable;<bus>;<dev>;<func>"
        else if (cmd == "msix_global_disable") {
            if (parts.size() < 4) { delete msg; continue; }

            const uint8_t bus  = static_cast<uint8_t>(stoul(parts[1]));
            const uint8_t dev  = static_cast<uint8_t>(stoul(parts[2]));
            const uint8_t func = static_cast<uint8_t>(stoul(parts[3]));

            msixGlobalDisable(bus, dev, func);
        } else {
            printf("PCI: Unknown command: %s\n", cmd.c_str());
        }

        delete msg;
    }

    return nullptr;
}