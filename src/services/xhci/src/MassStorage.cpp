#include "MassStorage.hpp"

#include "horizonos/generic.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>

namespace {
	auto allocateStage(AllocatedPage &page) -> bool {
		if (allocPhysPage(&page.phys) != 0) {
			return false;
		}

		if (mmap_phys(page.phys, STAGE_PAGE_SIZE, &page.virt, false) != 0) {
			freePhysPage(page.phys);
			page = {};

			return false;
		}

		memset(reinterpret_cast<void *>(page.virt), 0, STAGE_PAGE_SIZE);

		return true;
	}

	void freeStage(AllocatedPage &page) {
		if (page.virt != 0) {
			munmap_extra(reinterpret_cast<void *>(page.virt), STAGE_PAGE_SIZE, false);
		}

		if (page.phys != 0) {
			freePhysPage(page.phys);
		}

		page = {};
	}

	auto be32(const uint8_t *bytes) -> uint32_t {
		return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
	}

	void putBe32(uint8_t *bytes, const uint32_t value) {
		bytes[0] = static_cast<uint8_t>(value >> 24);
		bytes[1] = static_cast<uint8_t>(value >> 16);
		bytes[2] = static_cast<uint8_t>(value >> 8);
		bytes[3] = static_cast<uint8_t>(value);
	}
}

void UsbMassStorageDriver::setTransport(const UsbMassStorageTransport &nextTransport) {
	transport = nextTransport;
}

auto UsbMassStorageDriver::find(const uint32_t controllerId, const uint32_t nsid) -> Unit * {
	for (auto &unit : units) {
		if (unit.controllerId == controllerId and unit.nsid == nsid) {
			return &unit;
		}
	}

	return nullptr;
}

auto UsbMassStorageDriver::sendCommand(Unit &unit, const uint8_t *cdb, const uint8_t cdbLength, const uint64_t *dataPages, const uint32_t dataPageCount, const uint32_t dataLength, const bool dataIn) const -> bool {
	if (transport.bulkTransfer == nullptr or unit.device == nullptr or unit.bulkIn == nullptr or unit.bulkOut == nullptr or cdbLength == 0 or cdbLength > 16) {
		return false;
	}

	AllocatedPage cbwPage {};
	AllocatedPage cswPage {};

	if (!allocateStage(cbwPage) or !allocateStage(cswPage)) {
		freeStage(cbwPage);
		freeStage(cswPage);

		return false;
	}

	auto *cbw = reinterpret_cast<BotCbw *>(cbwPage.virt);

	cbw->signature = CBW_SIGNATURE;
	cbw->tag = unit.tag++;
	cbw->dataTransferLength = dataLength;
	cbw->flags = dataIn ? 0x80 : 0x00;
	cbw->lun = 0;
	cbw->cdbLength = cdbLength;

	memcpy(cbw->cdb, cdb, cdbLength);

	const uint64_t cbwPhys = cbwPage.phys;
	uint32_t actual = 0;
	bool ok = transport.bulkTransfer(transport.ctx, *unit.device, *unit.bulkOut, &cbwPhys, 1, sizeof(BotCbw), false, &actual) and actual == sizeof(BotCbw);

	if (ok and dataLength != 0) {
		UsbEndpoint &dataEndpoint = dataIn ? *unit.bulkIn : *unit.bulkOut;

		ok = transport.bulkTransfer(transport.ctx, *unit.device, dataEndpoint, dataPages, dataPageCount, dataLength, dataIn, &actual);
	}

	const uint64_t cswPhys = cswPage.phys;
	actual = 0;
	ok = ok and transport.bulkTransfer(transport.ctx, *unit.device, *unit.bulkIn, &cswPhys, 1, sizeof(BotCsw), true, &actual) and actual >= sizeof(BotCsw);

	if (ok) {
		const auto *csw = reinterpret_cast<const BotCsw *>(cswPage.virt);

		ok = csw->signature == CSW_SIGNATURE and csw->tag == cbw->tag and csw->status == 0;
	}

	freeStage(cbwPage);
	freeStage(cswPage);

	if (!ok) {
		recover(unit);
	}

	return ok;
}

auto UsbMassStorageDriver::inquiry(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!allocateStage(page)) {
		return false;
	}

	uint8_t cdb[6] {};

	cdb[0] = 0x12;
	cdb[4] = 36;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 36, true);

	if (ok) {
		const auto *data = reinterpret_cast<uint8_t *>(page.virt);
		char vendor[9] {};
		char product[17] {};

		memcpy(vendor, data + 8, 8);
		memcpy(product, data + 16, 16);

		printf("XHCI/MSD: INQUIRY slot=%u vendor='%s' product='%s'.", unit.device->slotId, vendor, product);
		fflush(stdout);
	}

	freeStage(page);

	return ok;
}

auto UsbMassStorageDriver::testUnitReady(Unit &unit) const -> bool {
	uint8_t cdb[6] {};

	cdb[0] = 0x00;

	return sendCommand(unit, cdb, sizeof(cdb), nullptr, 0, 0, false);
}

auto UsbMassStorageDriver::requestSense(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!allocateStage(page)) {
		return false;
	}

	uint8_t cdb[6] {};

	cdb[0] = 0x03;
	cdb[4] = 18;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 18, true);

	freeStage(page);

	return ok;
}

auto UsbMassStorageDriver::readCapacity10(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!allocateStage(page)) {
		return false;
	}

	uint8_t cdb[10] {};

	cdb[0] = 0x25;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 8, true);

	if (ok) {
		const auto *data = reinterpret_cast<uint8_t *>(page.virt);
		const uint32_t lastLba = be32(data);
		const uint32_t blockSize = be32(data + 4);

		if (lastLba == UINT32_MAX) {
			freeStage(page);
			return readCapacity16(unit);
		}

		unit.blockCount = static_cast<uint64_t>(lastLba) + 1;
		unit.blockSize = blockSize;

		printf("XHCI/MSD: READ CAPACITY slot=%u blocks=%lu blockSize=%u.", unit.device->slotId, unit.blockCount, unit.blockSize);
		fflush(stdout);
	}

	freeStage(page);

	return ok and unit.blockCount != 0 and unit.blockSize != 0;
}

auto UsbMassStorageDriver::readCapacity16(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!allocateStage(page)) {
		return false;
	}

	uint8_t cdb[16] {};

	cdb[0] = 0x9E;
	cdb[1] = 0x10;
	cdb[13] = 32;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 32, true);

	if (ok) {
		const auto *data = reinterpret_cast<uint8_t *>(page.virt);
		uint64_t lastLba = 0;

		for (uint32_t i = 0; i < 8; ++i) {
			lastLba = (lastLba << 8) | data[i];
		}

		unit.blockCount = lastLba + 1;
		unit.blockSize = be32(data + 8);

		printf("XHCI/MSD: READ CAPACITY(16) slot=%u blocks=%lu blockSize=%u.", unit.device->slotId, unit.blockCount, unit.blockSize);
		fflush(stdout);
	}

	freeStage(page);

	return ok and unit.blockCount != 0 and unit.blockSize != 0;
}

void UsbMassStorageDriver::recover(Unit &unit) const {
	if (transport.resetBulkOnly != nullptr and unit.device != nullptr) {
		transport.resetBulkOnly(transport.ctx, *unit.device, unit.interfaceNumber);
	}

	if (transport.clearEndpointHalt != nullptr and unit.device != nullptr and unit.bulkIn != nullptr) {
		transport.clearEndpointHalt(transport.ctx, *unit.device, *unit.bulkIn);
	}

	if (transport.clearEndpointHalt != nullptr and unit.device != nullptr and unit.bulkOut != nullptr) {
		transport.clearEndpointHalt(transport.ctx, *unit.device, *unit.bulkOut);
	}
}

auto UsbMassStorageDriver::readWrite10(Unit &unit, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount, const bool write) const -> bool {
	if (unit.blockSize == 0 or pageCount == 0 or pagePhysArray == nullptr) {
		return false;
	}

	const uint64_t byteCount = static_cast<uint64_t>(pageCount) * STAGE_PAGE_SIZE;

	if (byteCount % unit.blockSize != 0) {
		return false;
	}

	const uint64_t blocks = byteCount / unit.blockSize;

	if (lba > UINT32_MAX or blocks == 0 or blocks > 0xFFFF) {
		return false;
	}

	uint8_t cdb[10] {};

	cdb[0] = write ? 0x2A : 0x28;

	putBe32(cdb + 2, static_cast<uint32_t>(lba));

	cdb[7] = static_cast<uint8_t>(blocks >> 8);
	cdb[8] = static_cast<uint8_t>(blocks);

	return sendCommand(unit, cdb, sizeof(cdb), pagePhysArray, pageCount, static_cast<uint32_t>(byteCount), !write);
}

auto UsbMassStorageDriver::bind(const uint32_t controllerId, XhciDevice &device, UsbInterface &interface) -> bool {
	if (transport.registerBlockDevice == nullptr) {
		return false;
	}

	UsbEndpoint *bulkIn = nullptr;
	UsbEndpoint *bulkOut = nullptr;

	for (auto &endpoint : interface.endpoints) {
		const bool isBulk = (endpoint.attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK) == USB_ENDPOINT_TRANSFER_BULK;

		if (!isBulk) {
			continue;
		}

		if ((endpoint.address & 0x80U) != 0) {
			bulkIn = &endpoint;
		} else {
			bulkOut = &endpoint;
		}
	}

	if (bulkIn == nullptr or bulkOut == nullptr) {
		printf("XHCI/MSD: Mass storage interface slot=%u missing bulk endpoints.", device.slotId);
		fflush(stdout);

		return false;
	}

	Unit unit {};

	unit.controllerId = controllerId;
	unit.nsid = nextNsid++;
	unit.device = &device;
	unit.bulkIn = bulkIn;
	unit.bulkOut = bulkOut;
	unit.interfaceNumber = interface.number;

	if (!inquiry(unit)) {
		printf("XHCI/MSD: INQUIRY failed slot=%u.", device.slotId);
		fflush(stdout);

		return false;
	}

	for (int attempt = 0; attempt < 20; ++attempt) {
		if (testUnitReady(unit)) {
			break;
		}

		requestSense(unit);
		usleep(100000);
	}

	if (!readCapacity10(unit)) {
		printf("XHCI/MSD: READ CAPACITY failed slot=%u.", device.slotId);
		fflush(stdout);

		return false;
	}

	char name[32] {};

	snprintf(name, sizeof(name), "usb%un%u", controllerId, unit.nsid);

	if (!transport.registerBlockDevice(transport.ctx, unit.controllerId, unit.nsid, unit.blockCount, unit.blockSize, name, unit.storageDeviceId)) {
		printf("XHCI/MSD: StorageManager rejected %s.", name);
		fflush(stdout);

		return false;
	}

	printf("XHCI/MSD: Registered %s slot=%u blocks=%lu blockSize=%u.", name, device.slotId, unit.blockCount, unit.blockSize);
	fflush(stdout);

	units.push_back(unit);

	return true;
}

auto UsbMassStorageDriver::read(const uint32_t controllerId, const uint32_t nsid, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
	Unit *unit = find(controllerId, nsid);

	return unit != nullptr and readWrite10(*unit, lba, pagePhysArray, pageCount, false);
}

auto UsbMassStorageDriver::write(const uint32_t controllerId, const uint32_t nsid, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
	Unit *unit = find(controllerId, nsid);

	return unit != nullptr and readWrite10(*unit, lba, pagePhysArray, pageCount, true);
}

auto UsbMassStorageDriver::flush(const uint32_t controllerId, const uint32_t nsid) -> bool {
	Unit *unit = find(controllerId, nsid);

	if (unit == nullptr) {
		return false;
	}

	uint8_t cdb[10] {};

	cdb[0] = 0x35;

	return sendCommand(*unit, cdb, sizeof(cdb), nullptr, 0, 0, false);
}

void UsbMassStorageDriver::removeDevice(const XhciDevice &device) {
	for (auto it = units.begin(); it != units.end();) {
		if (it->device == &device) {
			if (transport.unregisterBlockDevice != nullptr and it->storageDeviceId != 0) {
				transport.unregisterBlockDevice(transport.ctx, it->storageDeviceId, it->controllerId, it->nsid);
			}

			it = units.erase(it);
			continue;
		}

		++it;
	}
}
