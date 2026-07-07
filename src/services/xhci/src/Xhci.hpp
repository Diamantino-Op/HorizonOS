#ifndef HORIZONOS_XHCI_HPP
#define HORIZONOS_XHCI_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t PCI_READ_MSG_TYPE = 0x20;
constexpr uint64_t PCI_READ_REPLY_MSG_TYPE = 0x30;
constexpr uint64_t PCI_WRITE_MSG_TYPE = 0x40;
constexpr uint64_t PCI_WRITE_REPLY_MSG_TYPE = 0x41;
constexpr uint64_t PCI_MSIX_ALLOC_MSG_TYPE = 0x80;
constexpr uint64_t PCI_MSIX_ALLOC_REPLY_MSG_TYPE = 0x90;
constexpr uint64_t PCI_MSIX_FREE_MSG_TYPE = 0xA0;
constexpr uint64_t PCI_MSIX_GLOBAL_ENABLE_MSG_TYPE = 0xB0;
constexpr uint64_t PCI_MSIX_GLOBAL_ENABLE_REPLY_MSG_TYPE = 0xB1;
constexpr uint64_t PCI_MSIX_GLOBAL_DISABLE_MSG_TYPE = 0xC0;
constexpr uint64_t PCI_MSIX_GLOBAL_DISABLE_REPLY_MSG_TYPE = 0xC1;
constexpr uint64_t PCI_SEARCH_DEVICE_MSG_TYPE = 0xD0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_START_MSG_TYPE = 0xE0;
constexpr uint64_t PCI_SEARCH_DEVICE_REPLY_MSG_TYPE = 0xF0;

constexpr uint64_t IRQ_RECEIVE_MSG_TYPE = 0x1000;
constexpr uint64_t USB_STORAGE_READ_MSG_BASE = 0x10000;
constexpr uint64_t USB_STORAGE_WRITE_MSG_BASE = 0x20000;
constexpr uint64_t USB_STORAGE_FLUSH_MSG_BASE = 0x30000;
constexpr uint64_t USB_STORAGE_REPLY_READ_MSG_BASE = 0x40000;
constexpr uint64_t USB_STORAGE_REPLY_WRITE_MSG_BASE = 0x50000;
constexpr uint64_t USB_STORAGE_REPLY_FLUSH_MSG_BASE = 0x60000;
constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_MSG_TYPE = 0x70000;
constexpr uint64_t STORAGE_REGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE = 0x70001;
constexpr uint64_t STORAGE_UNREGISTER_BLOCK_DEVICE_MSG_TYPE = 0x7000E;
constexpr uint64_t STORAGE_UNREGISTER_BLOCK_DEVICE_REPLY_MSG_TYPE = 0x7000F;

constexpr uint32_t STORAGE_MAX_PAGES_PER_MSG = 256;
constexpr uint32_t STORAGE_MAX_NAME_LENGTH = 32;
constexpr uint8_t STORAGE_TRANSPORT_GENERIC_BLOCK = 1;

constexpr uint8_t PCI_CLASS_SERIAL_BUS = 0x0C;
constexpr uint8_t PCI_SUBCLASS_USB = 0x03;
constexpr uint8_t PCI_PROGIF_XHCI = 0x30;

constexpr uint16_t PCI_COMMAND = 0x04;
constexpr uint16_t PCI_BAR0 = 0x10;
constexpr uint32_t PCI_COMMAND_MEMORY_SPACE = 1U << 1;
constexpr uint32_t PCI_COMMAND_BUS_MASTER = 1U << 2;

constexpr uint32_t XHCI_CAP_CAPLENGTH = 0x00;
constexpr uint32_t XHCI_CAP_HCSPARAMS1 = 0x04;
constexpr uint32_t XHCI_CAP_HCSPARAMS2 = 0x08;
constexpr uint32_t XHCI_CAP_HCCPARAMS1 = 0x10;
constexpr uint32_t XHCI_CAP_DBOFF = 0x14;
constexpr uint32_t XHCI_CAP_RTSOFF = 0x18;

constexpr uint32_t XHCI_OP_USBCMD = 0x00;
constexpr uint32_t XHCI_OP_USBSTS = 0x04;
constexpr uint32_t XHCI_OP_PAGESIZE = 0x08;
constexpr uint32_t XHCI_OP_CRCR = 0x18;
constexpr uint32_t XHCI_OP_DCBAAP = 0x30;
constexpr uint32_t XHCI_OP_CONFIG = 0x38;
constexpr uint32_t XHCI_OP_PORT_REGS = 0x400;
constexpr uint32_t XHCI_OP_PORT_STRIDE = 0x10;
constexpr uint32_t XHCI_PORTSC = 0x00;
constexpr uint32_t XHCI_PORTSC_CCS = 1U << 0;
constexpr uint32_t XHCI_PORTSC_PED = 1U << 1;
constexpr uint32_t XHCI_PORTSC_PR = 1U << 4;
constexpr uint32_t XHCI_PORTSC_SPEED_SHIFT = 10;
constexpr uint32_t XHCI_PORTSC_SPEED_MASK = 0xFU;
constexpr uint32_t XHCI_PORTSC_CSC = 1U << 17;
constexpr uint32_t XHCI_PORTSC_PEC = 1U << 18;
constexpr uint32_t XHCI_PORTSC_WRC = 1U << 19;
constexpr uint32_t XHCI_PORTSC_OCC = 1U << 20;
constexpr uint32_t XHCI_PORTSC_PRC = 1U << 21;
constexpr uint32_t XHCI_PORTSC_PLC = 1U << 22;
constexpr uint32_t XHCI_PORTSC_CEC = 1U << 23;
constexpr uint32_t XHCI_PORTSC_CHANGE_BITS = XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC;

constexpr uint32_t XHCI_USBCMD_RUN = 1U << 0;
constexpr uint32_t XHCI_USBCMD_HCRST = 1U << 1;
constexpr uint32_t XHCI_USBCMD_INTE = 1U << 2;
constexpr uint32_t XHCI_USBSTS_EINT = 1U << 3;
constexpr uint32_t XHCI_USBSTS_HCH = 1U << 0;
constexpr uint32_t XHCI_USBSTS_CNR = 1U << 11;

constexpr uint32_t XHCI_INTERRUPTER_IMAN = 0x00;
constexpr uint32_t XHCI_INTERRUPTER_IMOD = 0x04;
constexpr uint32_t XHCI_INTERRUPTER_ERSTSZ = 0x08;
constexpr uint32_t XHCI_INTERRUPTER_ERSTBA = 0x10;
constexpr uint32_t XHCI_INTERRUPTER_ERDP = 0x18;
constexpr uint32_t XHCI_INTERRUPTER_STRIDE = 0x20;
constexpr uint64_t XHCI_ERDP_EHB = 1ULL << 3;

constexpr uint32_t XHCI_TRB_TYPE_SHIFT = 10;
constexpr uint32_t XHCI_TRB_TYPE_MASK = 0x3F;
constexpr uint32_t XHCI_TRB_TYPE_NORMAL = 1;
constexpr uint32_t XHCI_TRB_TYPE_SETUP_STAGE = 2;
constexpr uint32_t XHCI_TRB_TYPE_DATA_STAGE = 3;
constexpr uint32_t XHCI_TRB_TYPE_STATUS_STAGE = 4;
constexpr uint32_t XHCI_TRB_TYPE_LINK = 6;
constexpr uint32_t XHCI_TRB_TYPE_ENABLE_SLOT_COMMAND = 9;
constexpr uint32_t XHCI_TRB_TYPE_DISABLE_SLOT_COMMAND = 10;
constexpr uint32_t XHCI_TRB_TYPE_ADDRESS_DEVICE_COMMAND = 11;
constexpr uint32_t XHCI_TRB_TYPE_CONFIGURE_ENDPOINT_COMMAND = 12;
constexpr uint32_t XHCI_TRB_TYPE_EVALUATE_CONTEXT_COMMAND = 13;
constexpr uint32_t XHCI_TRB_TYPE_RESET_ENDPOINT_COMMAND = 14;
constexpr uint32_t XHCI_TRB_TYPE_STOP_ENDPOINT_COMMAND = 15;
constexpr uint32_t XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER_COMMAND = 16;
constexpr uint32_t XHCI_TRB_TYPE_NOOP_COMMAND = 23;
constexpr uint32_t XHCI_TRB_TYPE_TRANSFER_EVENT = 32;
constexpr uint32_t XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT = 33;
constexpr uint32_t XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT = 34;
constexpr uint32_t XHCI_TRB_CYCLE = 1U << 0;
constexpr uint32_t XHCI_TRB_ENT = 1U << 1;
constexpr uint32_t XHCI_TRB_ISP = 1U << 2;
constexpr uint32_t XHCI_TRB_CHAIN = 1U << 4;
constexpr uint32_t XHCI_TRB_IOC = 1U << 5;
constexpr uint32_t XHCI_TRB_IDT = 1U << 6;
constexpr uint32_t XHCI_TRB_DIR_IN = 1U << 16;
constexpr uint32_t XHCI_TRB_TOGGLE_CYCLE = 1U << 1;
constexpr uint32_t XHCI_COMPLETION_SUCCESS = 1;
constexpr uint32_t XHCI_COMPLETION_SHORT_PACKET = 13;

constexpr uint32_t XHCI_PAGE_SIZE = 0x1000;
constexpr uint32_t XHCI_COMMAND_RING_TRBS = 256;
constexpr uint32_t XHCI_EVENT_RING_TRBS = 256;
constexpr uint32_t XHCI_TRANSFER_RING_TRBS = 256;
constexpr uint32_t XHCI_ERST_ENTRIES = 1;
constexpr uint32_t XHCI_MAX_CONFIGURED_SLOTS = 32;
constexpr uint32_t XHCI_CONTEXT_SIZE_32 = 32;
constexpr uint32_t XHCI_CONTEXT_SIZE_64 = 64;
constexpr uint32_t XHCI_INPUT_CONTROL_CONTEXT_INDEX = 0;
constexpr uint32_t XHCI_SLOT_CONTEXT_INDEX = 1;
constexpr uint32_t XHCI_EP0_CONTEXT_INDEX = 2;
constexpr uint32_t USB_DESCRIPTOR_DEVICE = 1;
constexpr uint32_t USB_DESCRIPTOR_CONFIGURATION = 2;
constexpr uint32_t USB_DESCRIPTOR_STRING = 3;
constexpr uint32_t USB_DESCRIPTOR_INTERFACE = 4;
constexpr uint32_t USB_DESCRIPTOR_ENDPOINT = 5;
constexpr uint32_t USB_DESCRIPTOR_HUB = 0x29;
constexpr uint32_t USB_REQUEST_GET_DESCRIPTOR = 6;
constexpr uint32_t USB_REQUEST_SET_CONFIGURATION = 9;
constexpr uint32_t USB_REQUEST_GET_STATUS = 0;
constexpr uint32_t USB_REQUEST_CLEAR_FEATURE = 1;
constexpr uint32_t USB_REQUEST_SET_FEATURE = 3;
constexpr uint32_t USB_HID_REQUEST_SET_IDLE = 0x0A;
constexpr uint32_t USB_HID_REQUEST_SET_PROTOCOL = 0x0B;
constexpr uint32_t USB_CLASS_HID = 0x03;
constexpr uint32_t USB_CLASS_HUB = 0x09;
constexpr uint32_t USB_CLASS_MASS_STORAGE = 0x08;
constexpr uint32_t USB_SUBCLASS_SCSI = 0x06;
constexpr uint32_t USB_PROTOCOL_BULK_ONLY = 0x50;
constexpr uint32_t USB_ENDPOINT_TRANSFER_TYPE_MASK = 0x03;
constexpr uint32_t USB_ENDPOINT_TRANSFER_CONTROL = 0x00;
constexpr uint32_t USB_ENDPOINT_TRANSFER_ISOCHRONOUS = 0x01;
constexpr uint32_t USB_ENDPOINT_TRANSFER_BULK = 0x02;
constexpr uint32_t USB_ENDPOINT_TRANSFER_INTERRUPT = 0x03;
constexpr uint32_t USB_HUB_FEATURE_PORT_RESET = 4;
constexpr uint32_t USB_HUB_FEATURE_PORT_POWER = 8;
constexpr uint32_t USB_HUB_FEATURE_C_PORT_CONNECTION = 16;
constexpr uint32_t USB_HUB_FEATURE_C_PORT_ENABLE = 17;
constexpr uint32_t USB_HUB_FEATURE_C_PORT_OVER_CURRENT = 19;
constexpr uint32_t USB_HUB_FEATURE_C_PORT_RESET = 20;
constexpr uint32_t USB_FEATURE_ENDPOINT_HALT = 0;
constexpr uint32_t USB_MASS_STORAGE_REQUEST_BULK_ONLY_RESET = 0xFF;
constexpr uint16_t USB_HUB_PORT_STATUS_CONNECTION = 1U << 0;
constexpr uint16_t USB_HUB_PORT_STATUS_ENABLE = 1U << 1;
constexpr uint16_t USB_HUB_PORT_STATUS_LOW_SPEED = 1U << 9;
constexpr uint16_t USB_HUB_PORT_STATUS_HIGH_SPEED = 1U << 10;

struct RegisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct RegisterReplyMsgData {
	bool success {};
};

struct CheckMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct CheckReplyMsgData {
	bool exists {};
};

struct GetMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct GetReplyMsgData {
	uint64_t port {};
	uint16_t tid {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct PciSearchDeviceMsgData {
	uint8_t pciClass {};
	uint8_t pciSubclass {};
	uint8_t pciProg {};
};

struct PciDevice {
	uint8_t bus {};
	uint8_t device {};
	uint8_t function {};
	uint16_t vendorId {};
	uint16_t deviceId {};
	uint8_t classCode {};
	uint8_t subclass {};
	uint8_t progIf {};
	uint8_t headerType {};
	bool isPcie {};
};

struct PciReadMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
};

struct PciReadReplyMsgData {
	uint32_t data {};
};

struct PciWriteMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
	uint32_t data {};
};

struct PciMsixAllocMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t idx {};
	uint64_t port {};
	uint64_t lapicId {};
};

struct PciMsixAllocReplyMsgData {
	uint8_t vec {};
};

struct PciMsixFreeMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t idx {};
	uint8_t vec {};
};

struct PciMsixGlobalEnableMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
};

struct PciMsixGlobalDisableMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
};

struct IrqReceiveData {
	uint64_t irqNum {};
	uint64_t cpuId {};
	bool isIrq {};
};

struct UsbStorageReadMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct UsbStorageReadReplyMsgData {
	uint64_t requestId {};
	bool success {};
	uint32_t pageCount {};
};

struct UsbStorageWriteMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t lba {};
	uint32_t pageCount {};
	uint64_t pagePhysArray[STORAGE_MAX_PAGES_PER_MSG] {};
};

struct UsbStorageWriteReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct UsbStorageFlushMsgData {
	uint64_t replyPort {};
	uint64_t requestId {};
	uint32_t controllerId {};
	uint32_t nsid {};
};

struct UsbStorageFlushReplyMsgData {
	uint64_t requestId {};
	bool success {};
};

struct StorageRegisterBlockDeviceMsgData {
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
	uint64_t blockCount {};
	uint32_t blockSize {};
	uint32_t maxPagesPerRequest {};
	char name[STORAGE_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint8_t transport {};
	uint64_t readMsgBase {};
	uint64_t writeMsgBase {};
	uint64_t flushMsgBase {};
	uint64_t readReplyMsgBase {};
	uint64_t writeReplyMsgBase {};
	uint64_t flushReplyMsgBase {};
};

struct StorageRegisterBlockDeviceReplyMsgData {
	bool success {};
	uint64_t deviceId {};
};

struct StorageUnregisterBlockDeviceMsgData {
	uint64_t deviceId {};
	uint64_t driverPort {};
	uint32_t controllerId {};
	uint32_t nsid {};
};

struct StorageUnregisterBlockDeviceReplyMsgData {
	bool success {};
	uint32_t removedCount {};
};

struct XhciTrb {
	uint32_t parameterLow {};
	uint32_t parameterHigh {};
	uint32_t status {};
	uint32_t control {};
};

struct XhciErstEntry {
	uint64_t ringSegmentBase {};
	uint32_t ringSegmentSize {};
	uint32_t reserved {};
};

struct AllocatedPage {
	uint64_t phys {};
	uint64_t virt {};
};

struct UsbEndpoint {
	uint8_t address {};
	uint8_t attributes {};
	uint16_t maxPacketSize {};
	uint8_t interval {};
	uint8_t endpointId {};
	uint8_t endpointType {};
	AllocatedPage transferRing {};
	uint32_t transferEnqueueIndex {};
	uint32_t transferProducerCycle { 1 };
};

struct UsbInterface {
	uint8_t number {};
	uint8_t alternateSetting {};
	uint8_t interfaceClass {};
	uint8_t interfaceSubclass {};
	uint8_t interfaceProtocol {};
	std::vector<UsbEndpoint> endpoints {};
};

struct ControllerMemory {
	AllocatedPage dcbaa {};
	AllocatedPage commandRing {};
	AllocatedPage eventRing {};
	AllocatedPage erst {};
	AllocatedPage scratchpadArray {};
	std::vector<AllocatedPage> scratchpads {};
	uint32_t commandProducerCycle { 1 };
	uint32_t commandEnqueueIndex {};
	uint32_t eventConsumerCycle { 1 };
	uint32_t eventDequeueIndex {};
	uint32_t maxScratchpads {};
	uint64_t eventPort {};
	uint8_t msixVector {};
};

struct XhciDevice {
	uint32_t controllerId {};
	uint8_t slotId {};
	uint8_t rootPort {};
	uint8_t parentSlotId {};
	uint8_t hubPort {};
	uint32_t routeString {};
	uint8_t depth {};
	uint8_t speed {};
	uint16_t maxPacketSize { 8 };
	uint8_t configurationValue {};
	bool configured {};
	bool isHub {};
	uint8_t hubPortCount {};
	uint16_t hubCharacteristics {};
	uint8_t hubPowerOnDelayMs {};
	uint8_t hubInterruptEndpointId {};
	uint8_t hubInterruptEndpointAddress {};
	bool hubInterruptTransferPending {};
	std::string manufacturer {};
	std::string product {};
	std::string serial {};
	std::vector<UsbInterface> interfaces {};
	AllocatedPage inputContext {};
	AllocatedPage deviceContext {};
	AllocatedPage transferRing {};
	AllocatedPage descriptorBuffer {};
	AllocatedPage hubInterruptBuffer {};
	uint32_t transferEnqueueIndex {};
	uint32_t transferProducerCycle { 1 };
};

struct MappedController {
	uint32_t controllerId {};
	PciDevice pci {};
	uint64_t barPhys {};
	uint64_t barSize {};
	uint64_t mmioVirt {};
	uint32_t originalCommand {};
	uint64_t operationalBase {};
	uint64_t runtimeBase {};
	uint64_t doorbellBase {};
	uint32_t maxSlots {};
	uint32_t maxPorts {};
	uint32_t maxInterrupters {};
	uint32_t configuredSlots {};
	bool uses64ByteContexts {};
	std::vector<XhciDevice> devices {};
	std::vector<std::pair<uint8_t, uint8_t>> pendingHubInterruptEvents {};
	ControllerMemory memory {};
};

#endif
