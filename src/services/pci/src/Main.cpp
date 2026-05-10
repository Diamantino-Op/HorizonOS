#include "abi-bits/hos_msg.h"
#include "horizonos/generic.h"
#include "Pci.hpp"

#include <cstdio>
#include <string>
#include <array>
#include <vector>
#include <thread>
#include <pthread.h>
#include <unistd.h>

using namespace std;

uint64_t pciPort = 0;
uint64_t uacpiPort = 0;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	// ── 1. Register port 3 ───────────────────────────────────────────────────
    if (const int r = register_horizonos_port(reinterpret_cast<long *>(&pciPort)); r != 0) {
        printf("PCI: Failed to register port: %d\n", r);

        return 1;
    }

    printf("PCI: Successfully registered port!\n");

    // ── 2. Register with name-registry ───────────────────────────────────────
    {
        auto *regMsg = new hos_msg();
        string msgStr = "register;" + to_string(getpid()) + ";" + to_string(hash<thread::id>{}(this_thread::get_id())) + ";" + "PCI;1;0;0";

        regMsg->port   = REGISTRY_PORT;
        regMsg->buffer = static_cast<void *>(msgStr.data());
        regMsg->length = msgStr.size();

        send_horizonos_message(pciPort, REGISTRY_PORT, regMsg);

        delete regMsg;

        array<char, 64> ackBuf{};
        auto *ackMsg = new hos_msg();

        ackMsg->buffer = ackBuf.data();
        ackMsg->length = ackBuf.size();

        const int srvRegResult = receive_horizonos_message(pciPort, ackMsg);
        const string retMsg(ackBuf.data(), static_cast<size_t>(ackMsg->ret_length));
        const int    retVal = stoi(retMsg);

        delete ackMsg;

        if (srvRegResult != 0 || retVal != 1) {
            printf("PCI: Failed to register service: %d\n", srvRegResult);

            return 1;
        }
    }

    printf("PCI: Successfully registered service!\n");

    // ── 3. Notify uACPI that PCI is ready (triggers MCFG forwarding) ─────────
    {
        auto *notifyMsg = new hos_msg();

        string notifyStr = "pci_ready;" + to_string(pciPort);

        notifyMsg->port   = UACPI_PORT;
        notifyMsg->buffer = static_cast<void *>(notifyStr.data());
        notifyMsg->length = notifyStr.size();

        send_horizonos_message(pciPort, UACPI_PORT, notifyMsg);

        delete notifyMsg;
    }

    printf("PCI: Notified uACPI (port %d), waiting for MCFG data...\n", UACPI_PORT);

    // ── 4. Receive mcfg_segment messages from uACPI ───────────────────────────
    // uACPI sends one "mcfg_segment;..." per entry, then "mcfg_done".
    {
        for (;;) {
            array<char, 256> buf{};
            auto *msg = new hos_msg();

            msg->buffer = buf.data();
            msg->length = buf.size();

            if (receive_horizonos_message(pciPort, msg) != 0) {
                delete msg;

                continue;
            }

            const string notice(buf.data(), static_cast<size_t>(msg->ret_length));
            delete msg;

            if (notice == "mcfg_done") {
                printf("PCI: All MCFG segments received (%zu segment(s))\n", g_ecamSegments.size());

                break;
            }

            if (notice.starts_with("mcfg_segment;")) {
                // Parse inline without going through the message loop.
                // Format: "mcfg_segment;<physBase>;<seg>;<startBus>;<endBus>"
                vector<string> parts;
                size_t start = 0;

                while (start <= notice.size()) {
                    const size_t sep = notice.find(';', start);

                    if (sep == string::npos) {
                        parts.emplace_back(notice.substr(start));

                        break;
                    }

                    parts.emplace_back(notice.substr(start, sep - start));
                    start = sep + 1;
                }

                if (parts.size() >= 5) {
                    addEcamSegment(stoull(parts[1]), static_cast<uint16_t>(stoul(parts[2])), static_cast<uint8_t>(stoul(parts[3])), static_cast<uint8_t>(stoul(parts[4])));
                }
            }
        }
    }

    // ── 5. Enumerate and log all devices ─────────────────────────────────────
    printf("PCI: Enumerating devices...\n");

    vector<PciDevice> devices;
    enumeratePci(devices);

    printf("PCI: Found %zu device(s):\n", devices.size());

    for (const auto &d : devices) {
        printf("  [%04x:%02x:%02x.%x]  Vendor=%04x  Device=%04x  "
               "Class=%02x:%02x  ProgIF=%02x  %s\n",
               0,           // segment group (extend later if needed)
               d.bus, d.device, d.function,
               d.vendorId, d.deviceId,
               d.classCode, d.subclass,
               d.progIf,
               d.isPcie ? "(PCIe/ECAM)" : "(PCI/legacy)");
    }

    // ── 6. Start the message-handling thread ──────────────────────────────────
    pthread_t msgThread;

    if (pthread_create(&msgThread, nullptr, pciMessageLoop, nullptr) != 0) {
        printf("PCI: Failed to create message loop thread\n");

        return 1;
    }

    pthread_detach(msgThread);

    // ── 7. Main thread idle loop ──────────────────────────────────────────────
    for (;;) {
        usleep(1000000);
    }

    return 0;
}