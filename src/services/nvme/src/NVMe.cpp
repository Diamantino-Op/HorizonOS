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
	// Read CC, clear EN bit (bit 0)
	auto ccReg = *reinterpret_cast<volatile uint32_t*>(mmioBase + 0x14);
	ccReg &= ~(1U << 0);
	*reinterpret_cast<volatile uint32_t*>(mmioBase + 0x14) = ccReg;

	// Poll CSTS.RDY until it clears (controller is disabled)
	for (int i = 0; i < 100000; ++i) {
		const auto csts = *reinterpret_cast<volatile uint32_t*>(mmioBase + 0x1C);

		if ((csts & 0x1U) == 0) {
			return true;
		}

		usleep(10000);
	}

	return false; // timeout
}

bool NvmeDriver::enableController() noexcept {
	auto cc = *reinterpret_cast<volatile uint32_t*>(mmioBase + 0x14);
	cc |= (1u << 0);        // Set EN
	cc |= (4u << 16);       // IOSQES = 6 (2^6 = 64 bytes per SQ entry)
	cc |= (4u << 20);       // IOCQES = 4 (2^4 = 16 bytes per CQ entry)
	*reinterpret_cast<volatile uint32_t*>(mmioBase + 0x14) = cc;

	// Poll CSTS.RDY = 1
	for (int i = 0; i < 100000; ++i) {
		auto csts = *reinterpret_cast<volatile uint32_t*>(mmioBase + 0x1C);

		if (csts & 0x1U) {
			return true;
		}

		usleep(10000);
	}

	return false; // timeout
}

// Configures the admin submission queue and admin completion queue.
bool NvmeDriver::initializeAdminQueues() noexcept {
	uint64_t adminSQPhys = 0;
	uint64_t adminCQPhys = 0;

	if (allocPhysPage(&adminSQPhys) != 0) {
		return false;
	}

	if (allocPhysPage(&adminCQPhys) != 0) {
		return false;
	}

	if (mmap_phys(adminSQPhys, 1, reinterpret_cast<uint64_t *>(this->adminSQ), false) != 0) {
		return false;
	}

	if (mmap_phys(adminCQPhys, 1, reinterpret_cast<uint64_t *>(this->adminCQ), false) != 0) {
		return false;
	}

	memset(this->adminSQ, 0, this->adminQDepth * sizeof(Command));
	memset(this->adminCQ, 0, this->adminQDepth * sizeof(CompletionEntry));

	// AQA (Admin Queue Attributes) at offset 0x24
	// Bits [27:16] = ACQS-1, Bits [11:0] = ASQS-1
	uint32_t aqa = ((adminQDepth - 1) << 16) | (adminQDepth - 1);
	*reinterpret_cast<volatile uint32_t*>(mmioBase + 0x24) = aqa;

	// ASQ (Admin Submission Queue Base Address) at offset 0x28 (64-bit)
	*reinterpret_cast<volatile uint64_t*>(mmioBase + 0x28) = adminSQPhys;

	// ACQ (Admin Completion Queue Base Address) at offset 0x30 (64-bit)
	*reinterpret_cast<volatile uint64_t*>(mmioBase + 0x30) = adminCQPhys;

	return true;
}

// Submits an admin command and retrieves the matching completion entry.
bool NvmeDriver::submitAdminCommand(const Command&, CompletionEntry&) noexcept {
	return false;
}

// Reads the controller's identification structure.
bool NvmeDriver::identifyController() noexcept {
	return false;
}

// Reads the identification data for a specific namespace.
bool NvmeDriver::identifyNamespace(std::uint32_t) noexcept {
	return false;
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