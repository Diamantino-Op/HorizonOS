#ifndef HORIZONOS_XHCI_MASS_STORAGE_HPP
#define HORIZONOS_XHCI_MASS_STORAGE_HPP

#include "Xhci.hpp"

#include <cstdint>
#include <vector>

constexpr uint32_t CBW_SIGNATURE = 0x43425355;
constexpr uint32_t CSW_SIGNATURE = 0x53425355;
constexpr uint32_t STAGE_PAGE_SIZE = 0x1000;

struct BotCbw {
	uint32_t signature {};
	uint32_t tag {};
	uint32_t dataTransferLength {};
	uint8_t flags {};
	uint8_t lun {};
	uint8_t cdbLength {};
	uint8_t cdb[16] {};
} __attribute__((packed));

struct BotCsw {
	uint32_t signature {};
	uint32_t tag {};
	uint32_t residue {};
	uint8_t status {};
} __attribute__((packed));

class MassStorageUtils {
public:
	static auto allocateStage(AllocatedPage &page) -> bool;
	static void freeStage(AllocatedPage &page);
	static auto be32(const uint8_t *bytes) -> uint32_t;
	static auto be64(const uint8_t *bytes) -> uint64_t;
	static void putBe16(uint8_t *bytes, uint16_t value);
	static void putBe32(uint8_t *bytes, uint32_t value);
	static void putBe64(uint8_t *bytes, uint64_t value);
};

struct UsbMassStorageTransport {
	void *ctx {};
	bool (*bulkTransfer)(const void *ctx, const XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, uint32_t pageCount, uint32_t length, bool in, uint32_t *actualLength) {};
	bool (*registerBlockDevice)(const void *ctx, uint32_t controllerId, uint32_t nsid, uint64_t blockCount, uint32_t blockSize, const char *name, uint64_t &deviceId) {};
	bool (*unregisterBlockDevice)(const void *ctx, uint64_t deviceId, uint32_t controllerId, uint32_t nsid) {};
	bool (*resetBulkOnly)(const void *ctx, XhciDevice &device, uint8_t interfaceNumber) {};
	bool (*clearEndpointHalt)(const void *ctx, XhciDevice &device, UsbEndpoint &endpoint) {};
};

class UsbMassStorageDriver {
public:
	void setTransport(const UsbMassStorageTransport &nextTransport);
	auto bind(uint32_t controllerId, XhciDevice &device, UsbInterface &interface) -> bool;
	auto read(uint32_t controllerId, uint32_t nsid, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	auto write(uint32_t controllerId, uint32_t nsid, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	auto flush(uint32_t controllerId, uint32_t nsid) -> bool;
	void removeDevice(const XhciDevice &device);

private:
	struct Unit {
		uint32_t controllerId {};
		uint32_t nsid {};
		XhciDevice *device {};
		UsbEndpoint *bulkIn {};
		UsbEndpoint *bulkOut {};
		uint8_t interfaceNumber {};
		uint64_t storageDeviceId {};
		uint64_t blockCount {};
		uint32_t blockSize {};
		uint32_t tag { 1 };
		uint8_t senseKey {};
		uint8_t additionalSenseCode {};
		uint8_t additionalSenseQualifier {};
	};

	UsbMassStorageTransport transport {};
	std::vector<Unit> units {};
	uint32_t nextNsid { 1 };

	auto find(uint32_t controllerId, uint32_t nsid) -> Unit *;
	auto sendCommand(Unit &unit, const uint8_t *cdb, uint8_t cdbLength, const uint64_t *dataPages, uint32_t dataPageCount, uint32_t dataLength, bool dataIn) const -> bool;
	auto inquiry(Unit &unit) const -> bool;
	auto testUnitReady(Unit &unit) const -> bool;
	auto requestSense(Unit &unit, bool logSense = false) const -> bool;
	auto readCapacity10(Unit &unit) const -> bool;
	auto readCapacity16(Unit &unit) const -> bool;
	void recover(Unit &unit) const;
	auto readWrite(Unit &unit, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount, bool write) const -> bool;
	auto readWriteWithRetry(Unit &unit, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount, bool write) const -> bool;
	auto flushWithRetry(Unit &unit) const -> bool;
};

#endif
