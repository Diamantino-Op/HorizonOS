#include "NVMe.hpp"

#include "horizonos/generic.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>

void *NvmeDriver::coreHandler(void *ctx) {
	auto *coreStruct = static_cast<CoreStruct *>(ctx);

	printf("NVMe %lu: Started handler for core!", coreStruct->cpuId);
	fflush(stdout);

	lockToCore(coreStruct->cpuId);

	printf("NVMe %lu: Locked to core!", coreStruct->cpuId);
	fflush(stdout);

	for (;;) {}
}

// Stores the controller MMIO base for later register access.
void NvmeDriver::attachRegisters(uint64_t* base, const uint64_t size, PciDevice *ownDevice) noexcept {
	this->mmioBase = base;
	this->mmioSize = size;
	this->device = ownDevice;
}

// Resets the controller and waits for it to report that it is ready.
bool NvmeDriver::resetController() noexcept {
	if (this->mmioBase == nullptr) {
		return false;
	}

	std::uint32_t cc = mmioRead32(this->mmioBase, 0x14); // CC
	cc &= ~(1u << 0); // EN = 0
	mmioWrite32(this->mmioBase, 0x14, cc);

	for (int i = 0; i < 100000; ++i) {
		const std::uint32_t csts = mmioRead32(this->mmioBase, 0x1C); // CSTS
		if ((csts & 0x1u) == 0) {
			return true;
		}

		usleep(10000);
	}

	return false;
}

bool NvmeDriver::enableController() noexcept {
	if (this->mmioBase == nullptr) {
		return false;
	}

	std::uint32_t cc = mmioRead32(this->mmioBase, 0x14); // CC
	cc |= (1u << 0);      // EN
	cc &= ~(0xFu << 16);  // clear IOSQES
	cc &= ~(0xFu << 20);  // clear IOCQES
	cc |= (6u << 16);     // IOSQES = 6 => 64-byte SQ entries
	cc |= (4u << 20);     // IOCQES = 4 => 16-byte CQ entries
	mmioWrite32(this->mmioBase, 0x14, cc);

	for (int i = 0; i < 100000; ++i) {
		const std::uint32_t csts = mmioRead32(this->mmioBase, 0x1C); // CSTS
		if ((csts & 0x1u) != 0) {
			return true;
		}

		usleep(10000);
	}

	return false;
}

// Configures the admin submission queue and admin completion queue.
bool NvmeDriver::initializeAdminQueues() noexcept {
	if (this->mmioBase == nullptr) {
		return false;
	}

	uint64_t adminSQPhys = 0;
	uint64_t adminCQPhys = 0;

	if (allocPhysPage(&adminSQPhys) != 0) {
		return false;
	}

	if (allocPhysPage(&adminCQPhys) != 0) {
		return false;
	}

	uint64_t adminSQVirt = 0;
	uint64_t adminCQVirt = 0;

	const uint64_t adminSQSize = this->adminQDepth * sizeof(Command);
	const uint64_t adminCQSize = this->adminQDepth * sizeof(CompletionEntry);

	if (mmap_phys(adminSQPhys, adminSQSize, &adminSQVirt, false) != 0) {
		return false;
	}

	if (mmap_phys(adminCQPhys, adminCQSize, &adminCQVirt, false) != 0) {
		return false;
	}

	this->adminSQ = reinterpret_cast<Command *>(adminSQVirt);
	this->adminCQ = reinterpret_cast<CompletionEntry *>(adminCQVirt);

	memset(this->adminSQ, 0, adminSQSize);
	memset(this->adminCQ, 0, adminCQSize);

	const uint32_t aqa = ((this->adminQDepth - 1) << 16) | ((this->adminQDepth - 1) <<  0);

	mmioWrite32(this->mmioBase, 0x24, aqa);         // AQA
	mmioWrite64(this->mmioBase, 0x28, adminSQPhys); // ASQ
	mmioWrite64(this->mmioBase, 0x30, adminCQPhys); // ACQ

	this->doorbellStride = 4U << ((mmioRead64(mmioBase, 0x00) >> 32) & 0xFU);

	return true;
}

// Submits an admin command and retrieves the matching completion entry.
bool NvmeDriver::submitAdminCommand(const Command& command, CompletionEntry& result) noexcept {
	Command entry = command;

	entry.cdw0.setCid(static_cast<uint16_t>(adminSQTail));
	adminSQ[adminSQTail] = entry;

	adminSQTail = (adminSQTail + 1) % adminQDepth;
	mmioWrite32(mmioBase, 0x1000, adminSQTail); // Admin SQ Tail Doorbell

	for (int i = 0; i < 100000; ++i) {
		const CompletionEntry& cqe = adminCQ[adminCQHead];

		if (cqe.status.phase == adminCQPhase) {
			result = cqe;

			adminCQHead = (adminCQHead + 1) % adminQDepth;

			if (adminCQHead == 0) {
				adminCQPhase ^= 1U;
			}

			mmioWrite32(mmioBase, 0x1000 + this->doorbellStride, adminCQHead);

			return result.status.statusCode == 0;
		}
	}

	return false;
}

// Reads the controller's identification structure.
bool NvmeDriver::identifyController() noexcept {
	uint64_t dataPhys = 0;

	if (allocPhysPage(&dataPhys) != 0) {
		return false;
	}

	uint64_t dataVirt = 0;

	if (mmap_phys(dataPhys, sizeof(IdentifyControllerData), &dataVirt, false) != 0) {
		return false;
	}

	memset(reinterpret_cast<void*>(dataVirt), 0, sizeof(IdentifyControllerData));

	Command cmd {};
	cmd.cdw0.setOpCode(0x06);           // Identify opcode
	cmd.nsid      = 0;                       // nsid = 0 for controller identify
	cmd.dptrLow   = dataPhys;                // PRP Entry 1: 4 KiB buffer
	cmd.dptrHigh  = 0;
	cmd.cdw10.raw = 0x01;                    // CNS = 01h → Identify Controller

	// 3. Submit and wait for completion
	CompletionEntry cqe {};

	if (not submitAdminCommand(cmd, cqe)) {
		return false;
	}

	// 4. Copy result into local storage
	memcpy(&controllerInfo, reinterpret_cast<void*>(dataVirt), sizeof(IdentifyControllerData));

	// Trim trailing spaces from model/serial for readable logging
	printf("NVMe: Controller model: %.40s, serial: %.20s, namespaces: %u\n", controllerInfo.mn, controllerInfo.sn, controllerInfo.nn);
	fflush(stdout);

	return true;
}

// Reads the identification data for a specific namespace.
bool NvmeDriver::identifyNamespace(uint32_t namespaceId) noexcept {
	// 1. Allocate a 4 KiB physical page for the DMA buffer
	uint64_t dataPhys = 0;

	if (allocPhysPage(&dataPhys) != 0) {
		return false;
	}

	uint64_t dataVirt = 0;

	if (mmap_phys(dataPhys, sizeof(IdentifyNamespaceData), &dataVirt, false) != 0) {
		return false;
	}

	memset(reinterpret_cast<void*>(dataVirt), 0, sizeof(IdentifyNamespaceData));

	// 2. Build the Identify command
	Command cmd {};
	cmd.cdw0.setOpCode(0x06);       // Identify opcode
	cmd.nsid      = namespaceId;         // Target namespace
	cmd.dptrLow   = dataPhys;
	cmd.dptrHigh  = 0;
	cmd.cdw10.raw = 0x00;               // CNS = 00h → Identify Namespace

	// 3. Submit
	CompletionEntry cqe {};

	if (not submitAdminCommand(cmd, cqe)) {
		return false;
	}

	// 4. Parse the result
	const auto* nsData = reinterpret_cast<IdentifyNamespaceData*>(dataVirt);

	// flbas[3:0] is the index of the active LBA format
	const uint8_t lbafIndex = nsData->flbas & 0x0Fu;
	const uint8_t lbads     = nsData->lbaf[lbafIndex].lbads; // power of 2

	NamespaceInfo info {};

	info.nsid      = namespaceId;
	info.totalLbas = nsData->nsze;
	info.lbaSize   = (lbads > 0) ? (1u << lbads) : 512u; // fallback to 512
	info.valid     = true;

	printf("NVMe: Namespace %u — %lu LBAs × %u bytes = %lu MB\n", namespaceId, info.totalLbas, info.lbaSize, (info.totalLbas * info.lbaSize) / (1024 * 1024));
	fflush(stdout);

	namespaces.push_back(info);

	return true;
}

// Issues a namespace read request.
bool NvmeDriver::read(std::uint32_t, std::uint64_t, void*, std::size_t) noexcept {
	return false;
}

// Issues a namespace write request.
bool NvmeDriver::write(std::uint32_t, std::uint64_t, const void*, std::size_t) noexcept {
	return false;
}

// Flushes outstanding writes for the selected namespace.
bool NvmeDriver::flush(std::uint32_t) noexcept {
	return false;
}

// Stops the controller and clears local driver state.
void NvmeDriver::shutdown() noexcept {
	mmioBase = nullptr;
}

uint32_t NvmeDriver::getNamespaceCount() const noexcept {
	return controllerInfo.nn;
}

uint32_t pciRead32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset) {
	auto sendMsg  = hos_msg();
	auto readData = PciReadMsgData { .bus = bus, .dev = dev, .func = func, .offset = offset, .width = 32 };

	sendMsg.type   = PCI_READ_MSG_TYPE;
	sendMsg.port   = pciPort;
	sendMsg.buffer = &readData;
	sendMsg.length = sizeof(PciReadMsgData);

	send_horizonos_message(nvmePort, pciPort, &sendMsg);

	auto recvMsg   = hos_msg();
	auto replyData = PciReadReplyMsgData{};

	recvMsg.buffer = &replyData;
	recvMsg.length = sizeof(PciReadReplyMsgData);

	auto filter = filter_options();

	filter.whiteListTypes = new uint64_t[1] { PCI_READ_REPLY_MSG_TYPE };
	filter.whiteListCount = 1;

	receive_horizonos_message(nvmePort, &recvMsg, &filter);

	delete[] filter.whiteListTypes;

	return replyData.data;
}

void pciWrite32(uint64_t nvmePort, uint64_t pciPort, uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t data) {
	auto sendMsg   = hos_msg();
	auto writeData = PciWriteMsgData { .bus = bus, .dev = dev, .func = func, .offset = offset, .width = 32, .data = data };

	sendMsg.type   = PCI_WRITE_MSG_TYPE;
	sendMsg.port   = pciPort;
	sendMsg.buffer = &writeData;
	sendMsg.length = sizeof(PciWriteMsgData);

	send_horizonos_message(nvmePort, pciPort, &sendMsg);

	// Recv

	auto recvMsg   = hos_msg();

	recvMsg.length = 0;

	auto filterOptions           = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_WRITE_REPLY_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	receive_horizonos_message(nvmePort, &recvMsg, &filterOptions);

	delete[] filterOptions.whiteListTypes;
}