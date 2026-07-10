#include "MassStorage.hpp"
#include "MassStorageRules.hpp"

#include "horizonos/generic.h"
#include "unistd.h"

#include <cstdio>
#include <cstring>

auto MassStorageUtils::allocateStage(AllocatedPage &page) -> bool {
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

	void MassStorageUtils::freeStage(AllocatedPage &page) {
		if (page.virt != 0) {
			munmap_extra(reinterpret_cast<void *>(page.virt), STAGE_PAGE_SIZE, false);
		}

		if (page.phys != 0) {
			freePhysPage(page.phys);
		}

		page = {};
	}

	auto MassStorageUtils::be32(const uint8_t *bytes) -> uint32_t {
		return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
	}

	auto MassStorageUtils::be64(const uint8_t *bytes) -> uint64_t {
		uint64_t value = 0;

		for (uint32_t i = 0; i < 8; ++i) {
			value = (value << 8) | bytes[i];
		}

		return value;
	}

	void MassStorageUtils::putBe16(uint8_t *bytes, const uint16_t value) {
		bytes[0] = static_cast<uint8_t>(value >> 8);
		bytes[1] = static_cast<uint8_t>(value);
	}

	void MassStorageUtils::putBe32(uint8_t *bytes, const uint32_t value) {
		bytes[0] = static_cast<uint8_t>(value >> 24);
		bytes[1] = static_cast<uint8_t>(value >> 16);
		bytes[2] = static_cast<uint8_t>(value >> 8);
		bytes[3] = static_cast<uint8_t>(value);
	}

	void MassStorageUtils::putBe64(uint8_t *bytes, const uint64_t value) {
		for (uint32_t i = 0; i < 8; ++i) {
			bytes[i] = static_cast<uint8_t>(value >> ((7 - i) * 8));
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

auto UsbMassStorageDriver::sendCommand(Unit &unit, const uint8_t *cdb, const uint8_t cdbLength, const uint64_t *dataPages, const uint32_t dataPageCount, const uint32_t dataLength, const bool dataIn, bool *scsiFailure) const -> bool {
	if (scsiFailure != nullptr) {
		*scsiFailure = false;
	}

	if (transport.bulkTransfer == nullptr or unit.device == nullptr or unit.bulkIn == nullptr or unit.bulkOut == nullptr or cdbLength == 0 or cdbLength > 16) {
		return false;
	}

	AllocatedPage cbwPage {};
	AllocatedPage cswPage {};

	if (!MassStorageUtils::allocateStage(cbwPage) or !MassStorageUtils::allocateStage(cswPage)) {
		MassStorageUtils::freeStage(cbwPage);
		MassStorageUtils::freeStage(cswPage);

		return false;
	}

	auto *cbw = reinterpret_cast<BotCbw *>(cbwPage.virt);
	const uint32_t tag = unit.tag++;

	cbw->signature = CBW_SIGNATURE;
	cbw->tag = tag;
	cbw->dataTransferLength = dataLength;
	cbw->flags = dataIn ? 0x80 : 0x00;
	cbw->lun = 0;
	cbw->cdbLength = cdbLength;

	memcpy(cbw->cdb, cdb, cdbLength);

	const uint64_t cbwPhys = cbwPage.phys;
	uint32_t actual = 0;
	bool transportOk = transport.bulkTransfer(transport.ctx, *unit.device, *unit.bulkOut, &cbwPhys, 1, sizeof(BotCbw), false, &actual) and actual == sizeof(BotCbw);
	uint32_t dataActual = 0;

	if (transportOk and dataLength != 0) {
		UsbEndpoint &dataEndpoint = dataIn ? *unit.bulkIn : *unit.bulkOut;

		transportOk = transport.bulkTransfer(transport.ctx, *unit.device, dataEndpoint, dataPages, dataPageCount, dataLength, dataIn, &dataActual);
	}

	const uint64_t cswPhys = cswPage.phys;
	actual = 0;
	const bool cswRead = transportOk and transport.bulkTransfer(transport.ctx, *unit.device, *unit.bulkIn, &cswPhys, 1, sizeof(BotCsw), true, &actual) and actual == sizeof(BotCsw);
	bool commandPassed = false;
	bool recoveryRequired = !cswRead;

	if (cswRead) {
		const auto *csw = reinterpret_cast<const BotCsw *>(cswPage.virt);
		const bool validCsw = csw->signature == CSW_SIGNATURE and csw->tag == tag and csw->status <= 2 and csw->residue <= dataLength;

		if (!validCsw) {
			printf("XHCI/MSD: Invalid CSW slot=%u signature=0x%x tag=0x%x expected=0x%x residue=%u status=%u.",
			       unit.device->slotId,
			       csw->signature,
			       csw->tag,
			       tag,
			       csw->residue,
			       csw->status);
			fflush(stdout);
			recoveryRequired = true;
		} else if (csw->status == 2) {
			printf("XHCI/MSD: BOT phase error slot=%u tag=0x%x residue=%u.", unit.device->slotId, tag, csw->residue);
			fflush(stdout);
			recoveryRequired = true;
		} else if (csw->status == 0) {
			const bool consistentLength = dataLength == 0
				? csw->residue == 0
				: dataActual <= dataLength and csw->residue == dataLength - dataActual;

			commandPassed = consistentLength;
			recoveryRequired = !consistentLength;

			if (!consistentLength) {
				printf("XHCI/MSD: BOT length mismatch slot=%u tag=0x%x requested=%u actual=%u residue=%u.",
				       unit.device->slotId,
				       tag,
				       dataLength,
				       dataActual,
				       csw->residue);
				fflush(stdout);
			}
		} else if (scsiFailure != nullptr) {
			*scsiFailure = true;
		}
		// CSW status 1 is a normal SCSI command failure. The caller will issue
		// REQUEST SENSE; BOT reset recovery is reserved for transport/phase errors.
	}

	MassStorageUtils::freeStage(cbwPage);
	MassStorageUtils::freeStage(cswPage);

	if (recoveryRequired) {
		recover(unit);
	}

	return commandPassed;
}

auto UsbMassStorageDriver::inquiry(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!MassStorageUtils::allocateStage(page)) {
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

	MassStorageUtils::freeStage(page);

	return ok;
}

auto UsbMassStorageDriver::testUnitReady(Unit &unit) const -> bool {
	uint8_t cdb[6] {};

	cdb[0] = 0x00;

	return sendCommand(unit, cdb, sizeof(cdb), nullptr, 0, 0, false);
}

auto UsbMassStorageDriver::requestSense(Unit &unit, const bool logSense) const -> bool {
	AllocatedPage page {};

	if (!MassStorageUtils::allocateStage(page)) {
		return false;
	}

	uint8_t cdb[6] {};

	cdb[0] = 0x03;
	cdb[4] = 18;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 18, true);

	if (ok) {
		const auto *data = reinterpret_cast<uint8_t *>(page.virt);

		unit.senseKey = data[2] & 0x0F;
		unit.additionalSenseCode = data[12];
		unit.additionalSenseQualifier = data[13];

		if (logSense) {
			printf("XHCI/MSD: REQUEST SENSE slot=%u key=0x%02x asc=0x%02x ascq=0x%02x.", unit.device != nullptr ? unit.device->slotId : 0, unit.senseKey, unit.additionalSenseCode, unit.additionalSenseQualifier);
			fflush(stdout);
		}
	}

	MassStorageUtils::freeStage(page);

	return ok;
}

auto UsbMassStorageDriver::readCapacity10(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!MassStorageUtils::allocateStage(page)) {
		return false;
	}

	uint8_t cdb[10] {};

	cdb[0] = 0x25;

	const uint64_t phys = page.phys;
	const bool ok = sendCommand(unit, cdb, sizeof(cdb), &phys, 1, 8, true);

	if (ok) {
		const auto *data = reinterpret_cast<uint8_t *>(page.virt);
		const uint32_t lastLba = MassStorageUtils::be32(data);
		const uint32_t blockSize = MassStorageUtils::be32(data + 4);

		if (lastLba == UINT32_MAX) {
			MassStorageUtils::freeStage(page);
			return readCapacity16(unit);
		}

		unit.blockCount = static_cast<uint64_t>(lastLba) + 1;
		unit.blockSize = blockSize;

		printf("XHCI/MSD: READ CAPACITY slot=%u blocks=%lu blockSize=%u.", unit.device->slotId, unit.blockCount, unit.blockSize);
		fflush(stdout);
	}

	MassStorageUtils::freeStage(page);

	return ok and mass_storage_rules::validCapacity(unit.blockCount, unit.blockSize, STAGE_PAGE_SIZE);
}

auto UsbMassStorageDriver::readCapacity16(Unit &unit) const -> bool {
	AllocatedPage page {};

	if (!MassStorageUtils::allocateStage(page)) {
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
		const uint64_t lastLba = MassStorageUtils::be64(data);

		unit.blockCount = lastLba + 1;
		unit.blockSize = MassStorageUtils::be32(data + 8);

		printf("XHCI/MSD: READ CAPACITY(16) slot=%u blocks=%lu blockSize=%u.", unit.device->slotId, unit.blockCount, unit.blockSize);
		fflush(stdout);
	}

	MassStorageUtils::freeStage(page);

	return ok and mass_storage_rules::validCapacity(unit.blockCount, unit.blockSize, STAGE_PAGE_SIZE);
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

auto UsbMassStorageDriver::readWrite(Unit &unit, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount, const bool write) const -> bool {
	if (unit.blockSize == 0 or pageCount == 0 or pagePhysArray == nullptr) {
		return false;
	}

	const uint64_t byteCount = static_cast<uint64_t>(pageCount) * STAGE_PAGE_SIZE;

	if (byteCount % unit.blockSize != 0) {
		return false;
	}

	const uint64_t blocks = byteCount / unit.blockSize;

	if (blocks == 0 or blocks > UINT32_MAX or lba >= unit.blockCount or blocks > unit.blockCount - lba) {
		return false;
	}

	if (lba <= UINT32_MAX and blocks <= 0xFFFF and blocks - 1 <= UINT32_MAX - lba) {
		uint8_t cdb[10] {};

		cdb[0] = write ? 0x2A : 0x28;
		MassStorageUtils::putBe32(cdb + 2, static_cast<uint32_t>(lba));
		MassStorageUtils::putBe16(cdb + 7, static_cast<uint16_t>(blocks));

		return sendCommand(unit, cdb, sizeof(cdb), pagePhysArray, pageCount, static_cast<uint32_t>(byteCount), !write);
	}

	uint8_t cdb[16] {};

	cdb[0] = write ? 0x8A : 0x88;
	MassStorageUtils::putBe64(cdb + 2, lba);
	MassStorageUtils::putBe32(cdb + 10, static_cast<uint32_t>(blocks));

	return sendCommand(unit, cdb, sizeof(cdb), pagePhysArray, pageCount, static_cast<uint32_t>(byteCount), !write);
}

auto UsbMassStorageDriver::readWriteWithRetry(Unit &unit, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount, const bool write) const -> bool {
	for (uint32_t attempt = 0; attempt < 3; ++attempt) {
		if (readWrite(unit, lba, pagePhysArray, pageCount, write)) {
			return true;
		}

		requestSense(unit, true);

		if (unit.senseKey == 0x06) {
			readCapacity10(unit);
		}

		if (unit.senseKey == 0x03 or unit.senseKey == 0x04) {
			break;
		}

		usleep(50000);
	}

	printf("XHCI/MSD: %s failed slot=%u lba=%lu pages=%u key=0x%02x asc=0x%02x ascq=0x%02x.",
	       write ? "WRITE" : "READ",
	       unit.device == nullptr ? 0 : unit.device->slotId,
	       lba,
	       pageCount,
	       unit.senseKey,
	       unit.additionalSenseCode,
	       unit.additionalSenseQualifier);
	fflush(stdout);

	return false;
}

auto UsbMassStorageDriver::flushWithRetry(Unit &unit) const -> bool {
	if (!unit.synchronizeCacheSupported) {
		return true;
	}

	uint8_t cdb[10] {};

	cdb[0] = 0x35;

	for (uint32_t attempt = 0; attempt < 3; ++attempt) {
		bool scsiFailure = false;

		if (sendCommand(unit, cdb, sizeof(cdb), nullptr, 0, 0, false, &scsiFailure)) {
			return true;
		}

		const bool senseOk = requestSense(unit, true);

		if (scsiFailure and senseOk and
		    (unit.senseKey == 0x05 or
		     (unit.senseKey == 0 and unit.additionalSenseCode == 0 and unit.additionalSenseQualifier == 0))) {
			unit.synchronizeCacheSupported = false;
			printf("XHCI/MSD: SYNCHRONIZE CACHE unsupported slot=%u; disabling explicit flushes.", unit.device != nullptr ? unit.device->slotId : 0);
			fflush(stdout);
			return true;
		}

		usleep(50000);
	}

	return false;
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
	unit.device = &device;
	unit.bulkIn = bulkIn;
	unit.bulkOut = bulkOut;
	unit.interfaceNumber = interface.number;

	if (!inquiry(unit)) {
		printf("XHCI/MSD: INQUIRY failed slot=%u.", device.slotId);
		fflush(stdout);

		return false;
	}

	bool ready = false;

	for (int attempt = 0; attempt < 20; ++attempt) {
		if (testUnitReady(unit)) {
			ready = true;
			break;
		}

		requestSense(unit);
		usleep(100000);
	}

	if (!ready) {
		printf("XHCI/MSD: Unit not ready slot=%u.", device.slotId);
		fflush(stdout);

		return false;
	}

	if (!readCapacity10(unit)) {
		printf("XHCI/MSD: READ CAPACITY failed slot=%u.", device.slotId);
		fflush(stdout);

		return false;
	}

	{
		const std::scoped_lock lock(unitsMutex);
		unit.nsid = nextNsid++;
	}

	char name[32] {};

	snprintf(name, sizeof(name), "usb%u", unit.nsid - 1);

	// StorageManager probes the device synchronously as part of registration.
	// Publish the fully initialized unit first so those reads can resolve it.
	{
		const std::scoped_lock lock(unitsMutex);
		units.push_back(unit);
	}

	uint64_t storageDeviceId = 0;

	if (!transport.registerBlockDevice(transport.ctx, unit.controllerId, unit.nsid, unit.blockCount, unit.blockSize, name, storageDeviceId)) {
		const std::scoped_lock lock(unitsMutex);

		for (auto it = units.begin(); it != units.end(); ++it) {
			if (it->controllerId == unit.controllerId and it->nsid == unit.nsid) {
				units.erase(it);
				break;
			}
		}

		printf("XHCI/MSD: StorageManager rejected %s.", name);
		fflush(stdout);

		return false;
	}

	bool publishedStillPresent = false;

	{
		const std::scoped_lock lock(unitsMutex);
		Unit *published = find(unit.controllerId, unit.nsid);

		if (published != nullptr) {
			published->storageDeviceId = storageDeviceId;
			publishedStillPresent = true;
		}
	}

	if (!publishedStillPresent) {
		if (transport.unregisterBlockDevice != nullptr) {
			transport.unregisterBlockDevice(transport.ctx, storageDeviceId, unit.controllerId, unit.nsid);
		}

		return false;
	}

	printf("XHCI/MSD: Registered %s slot=%u blocks=%lu blockSize=%u.", name, device.slotId, unit.blockCount, unit.blockSize);
	fflush(stdout);

	return true;
}

auto UsbMassStorageDriver::read(const uint32_t controllerId, const uint32_t nsid, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
	const std::scoped_lock lock(unitsMutex);
	Unit *unit = find(controllerId, nsid);

	return unit != nullptr and readWriteWithRetry(*unit, lba, pagePhysArray, pageCount, false);
}

auto UsbMassStorageDriver::write(const uint32_t controllerId, const uint32_t nsid, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount) -> bool {
	const std::scoped_lock lock(unitsMutex);
	Unit *unit = find(controllerId, nsid);

	return unit != nullptr and readWriteWithRetry(*unit, lba, pagePhysArray, pageCount, true);
}

auto UsbMassStorageDriver::flush(const uint32_t controllerId, const uint32_t nsid) -> bool {
	const std::scoped_lock lock(unitsMutex);
	Unit *unit = find(controllerId, nsid);

	if (unit == nullptr) {
		return false;
	}

	return flushWithRetry(*unit);
}

void UsbMassStorageDriver::removeDevice(const XhciDevice &device) {
	std::vector<Unit> removedUnits;

	{
		const std::scoped_lock lock(unitsMutex);

		for (auto it = units.begin(); it != units.end();) {
			if (it->device == &device) {
				removedUnits.push_back(*it);
				it = units.erase(it);
				continue;
			}

			++it;
		}
	}

	for (const auto &unit : removedUnits) {
		if (transport.unregisterBlockDevice != nullptr and unit.storageDeviceId != 0) {
			transport.unregisterBlockDevice(transport.ctx, unit.storageDeviceId, unit.controllerId, unit.nsid);
		}
	}
}
