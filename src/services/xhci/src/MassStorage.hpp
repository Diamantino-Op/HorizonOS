#ifndef HORIZONOS_XHCI_MASS_STORAGE_HPP
#define HORIZONOS_XHCI_MASS_STORAGE_HPP

#include "Xhci.hpp"

#include <cstdint>
#include <vector>

struct UsbMassStorageTransport {
	void *ctx {};
	bool (*bulkTransfer)(void *ctx, XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, uint32_t pageCount, uint32_t length, bool in, uint32_t *actualLength) {};
	bool (*registerBlockDevice)(void *ctx, uint32_t controllerId, uint32_t nsid, uint64_t blockCount, uint32_t blockSize, const char *name) {};
};

class UsbMassStorageDriver {
public:
	void setTransport(UsbMassStorageTransport transport);
	auto bind(uint32_t controllerId, XhciDevice &device, UsbInterface &interface) -> bool;
	auto read(uint32_t controllerId, uint32_t nsid, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	auto write(uint32_t controllerId, uint32_t nsid, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount) -> bool;
	auto flush(uint32_t controllerId, uint32_t nsid) -> bool;

private:
	struct Unit {
		uint32_t controllerId {};
		uint32_t nsid {};
		XhciDevice *device {};
		UsbEndpoint *bulkIn {};
		UsbEndpoint *bulkOut {};
		uint64_t blockCount {};
		uint32_t blockSize {};
		uint32_t tag { 1 };
	};

	UsbMassStorageTransport transport {};
	std::vector<Unit> units {};
	uint32_t nextNsid { 1 };

	auto find(uint32_t controllerId, uint32_t nsid) -> Unit *;
	auto sendCommand(Unit &unit, const uint8_t *cdb, uint8_t cdbLength, const uint64_t *dataPages, uint32_t dataPageCount, uint32_t dataLength, bool dataIn) -> bool;
	auto inquiry(Unit &unit) -> bool;
	auto testUnitReady(Unit &unit) -> bool;
	auto requestSense(Unit &unit) -> bool;
	auto readCapacity10(Unit &unit) -> bool;
	auto readWrite10(Unit &unit, uint64_t lba, const uint64_t *pagePhysArray, uint32_t pageCount, bool write) -> bool;
};

#endif
