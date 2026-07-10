#ifndef HORIZONOS_XHCI_HPP
#define HORIZONOS_XHCI_HPP

#include <cstddef>
#include <cstdint>
#include <list>
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
constexpr uint64_t PCI_MSI_ALLOC_MSG_TYPE = 0x50;
constexpr uint64_t PCI_MSI_ALLOC_REPLY_MSG_TYPE = 0x60;
constexpr uint64_t PCI_MSI_FREE_MSG_TYPE = 0x70;
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
constexpr uint16_t PCI_INTERRUPT_LINE = 0x3C;
constexpr uint16_t PCI_INTERRUPT_PIN = 0x3D;
constexpr uint32_t PCI_COMMAND_MEMORY_SPACE = 1U << 1;
constexpr uint32_t PCI_COMMAND_BUS_MASTER = 1U << 2;
constexpr uint32_t PCI_COMMAND_INTERRUPT_DISABLE = 1U << 10;

constexpr uint32_t XHCI_CAP_CAPLENGTH = 0x00;
constexpr uint32_t XHCI_CAP_HCSPARAMS1 = 0x04;
constexpr uint32_t XHCI_CAP_HCSPARAMS2 = 0x08;
constexpr uint32_t XHCI_CAP_HCCPARAMS1 = 0x10;
constexpr uint32_t XHCI_CAP_DBOFF = 0x14;
constexpr uint32_t XHCI_CAP_RTSOFF = 0x18;
constexpr uint32_t XHCI_EXT_CAP_USB_LEGACY_SUPPORT = 1;
constexpr uint32_t XHCI_EXT_CAP_SUPPORTED_PROTOCOL = 2;
constexpr uint32_t XHCI_LEGACY_BIOS_OWNED = 1U << 16;
constexpr uint32_t XHCI_LEGACY_OS_OWNED = 1U << 24;

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
constexpr uint32_t XHCI_PORTSC_PP = 1U << 9;
constexpr uint32_t XHCI_PORTSC_WPR = 1U << 31;
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
constexpr uint32_t XHCI_ENDPOINT_STATE_DISABLED = 0;
constexpr uint32_t XHCI_ENDPOINT_STATE_RUNNING = 1;
constexpr uint32_t XHCI_ENDPOINT_STATE_HALTED = 2;
constexpr uint32_t XHCI_ENDPOINT_STATE_STOPPED = 3;
constexpr uint32_t XHCI_ENDPOINT_STATE_ERROR = 4;

constexpr uint32_t XHCI_PAGE_SIZE = 0x1000;
constexpr uint32_t XHCI_COMMAND_RING_TRBS = 256;
constexpr uint32_t XHCI_EVENT_RING_TRBS = 256;
constexpr uint32_t XHCI_TRANSFER_RING_TRBS = 256;
// One entry in each transfer ring is reserved for its Link TRB.
constexpr uint32_t XHCI_MAX_TRANSFER_PAGES = XHCI_TRANSFER_RING_TRBS - 1;
constexpr uint32_t XHCI_CONTROL_TRANSFER_ATTEMPTS = 4;
constexpr uint32_t XHCI_ERST_ENTRIES = 1;
constexpr uint32_t XHCI_MAX_CONFIGURED_SLOTS = 32;
constexpr uint32_t XHCI_CONTEXT_SIZE_32 = 32;
constexpr uint32_t XHCI_CONTEXT_SIZE_64 = 64;
constexpr uint32_t XHCI_INPUT_CONTROL_CONTEXT_INDEX = 0;
constexpr uint32_t XHCI_SLOT_CONTEXT_INDEX = 1;
constexpr uint32_t XHCI_EP0_CONTEXT_INDEX = 2;
constexpr uint32_t XHCI_EP0_DWORD1_DEFAULT = (4U << 3) | (3U << 1);
constexpr uint32_t USB_DESCRIPTOR_DEVICE = 1;
constexpr uint32_t USB_DESCRIPTOR_CONFIGURATION = 2;
constexpr uint32_t USB_DESCRIPTOR_STRING = 3;
constexpr uint32_t USB_DESCRIPTOR_INTERFACE = 4;
constexpr uint32_t USB_DESCRIPTOR_ENDPOINT = 5;
constexpr uint32_t USB_DESCRIPTOR_HUB = 0x29;
constexpr uint32_t USB_DESCRIPTOR_SS_HUB = 0x2A;
constexpr uint32_t USB_DESCRIPTOR_SS_ENDPOINT_COMPANION = 0x30;
constexpr uint32_t USB_REQUEST_GET_DESCRIPTOR = 6;
constexpr uint32_t USB_REQUEST_SET_CONFIGURATION = 9;
constexpr uint32_t USB_REQUEST_SET_HUB_DEPTH = 12;
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

struct PciMsiAllocMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint64_t port {};
	uint64_t lapicId {};
};

struct PciMsiAllocReplyMsgData {
	uint8_t vec {};
};

struct PciMsiFreeMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
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
	uint8_t maxBurst {};
	uint8_t mult {};
	uint32_t maxEsitPayload {};
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
	bool massStorageBound {};
	uint8_t massStorageBindAttempts {};
	std::vector<UsbEndpoint> endpoints {};
};

struct HidInterruptPipe {
	uint8_t interfaceNumber {};
	uint8_t protocol {};
	uint8_t endpointId {};
	uint8_t endpointAddress {};
	uint16_t reportLength {};
	bool transferPending {};
	AllocatedPage reportBuffer {};
	uint8_t previousReport[8] {};
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
	uint8_t msiVector {};
	uint8_t legacyIrq {};
	bool usingMsix {};
	bool usingMsi {};
	bool usingLegacyIrq {};
};

struct XhciDevice {
	uint32_t controllerId {};
	uint8_t slotId {};
	uint8_t rootPort {};
	uint8_t parentSlotId {};
	uint8_t hubPort {};
	uint8_t ttHubSlotId {};
	uint8_t ttPortNumber {};
	uint32_t routeString {};
	uint8_t depth {};
	uint8_t speed {};
	uint16_t maxPacketSize { 8 };
	uint8_t configurationValue {};
	bool configured {};
	bool isHub {};
	bool multiTT {};
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
	std::vector<HidInterruptPipe> hidInterruptPipes {};
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
	uint64_t dmaAddressLimit {};
	bool uses64ByteContexts {};
	bool started {};
	std::vector<uint8_t> rootPortProtocolMajor {};
	std::list<XhciDevice> devices {};
	std::vector<XhciTrb> pendingTransferEvents {};
	ControllerMemory memory {};
};

class XhciService {
public:
	auto start() -> int;
};

class XhciUtils {
public:
	static void fillName(char *dst, size_t dstSize, size_t &length, const std::string &name);
	static auto mmioRead8(uint64_t base, uint32_t offset) -> uint8_t;
	static auto mmioRead32(uint64_t base, uint32_t offset) -> uint32_t;
	static void mmioWrite32(uint64_t base, uint32_t offset, uint32_t value);
	static void mmioWrite64(uint64_t base, uint32_t offset, uint64_t value);
	static void dmaReadFence();
	static void dmaWriteFence();
	static auto allocatePage(AllocatedPage &page, uint64_t maxPhysExclusive = 0) -> bool;
	static void freePage(AllocatedPage &page);
	static void releaseControllerMemory(ControllerMemory &memory);
	static auto registerWithNameRegistry() -> bool;
	static auto waitForService(const char *name) -> GetReplyMsgData;
	static auto pciRead32(const PciDevice &dev, uint16_t offset) -> uint32_t;
	static void pciWrite32(const PciDevice &dev, uint16_t offset, uint32_t value);
	static void msixGlobalEnable(const PciDevice &dev);
	static auto msixAllocVector(const PciDevice &dev, uint16_t tableIndex, uint64_t notifyPort, uint64_t lapicId = 1000000) -> uint8_t;
	static auto msiAllocVector(const PciDevice &dev, uint64_t notifyPort, uint64_t lapicId = 1000000) -> uint8_t;
	static auto findControllers() -> std::vector<PciDevice>;
	static auto mapBar0(const PciDevice &dev, MappedController &controller) -> bool;
	static void takeBiosOwnership(uint64_t capabilityBase, uint32_t hccParams1);
	static void discoverRootPortProtocols(MappedController &controller, uint64_t capabilityBase, uint32_t hccParams1);
	static auto waitForControllerReady(uint64_t operationalBase) -> bool;
	static auto haltController(uint64_t operationalBase) -> bool;
	static auto resetController(uint64_t operationalBase) -> bool;
	static auto startController(uint64_t operationalBase, bool interruptsEnabled) -> bool;
	static void setControllerInterruptsEnabled(uint64_t operationalBase, bool enabled);
	static void powerRootPorts(MappedController &controller);
	static auto maxScratchpadBuffers(uint32_t hcsParams2) -> uint32_t;
	static auto setupScratchpads(ControllerMemory &memory, uint32_t maxScratchpads, uint64_t dmaAddressLimit) -> bool;
	static auto setupCommandRing(ControllerMemory &memory, uint64_t operationalBase, uint64_t dmaAddressLimit) -> bool;
	static auto setupEventRing(ControllerMemory &memory, uint64_t runtimeBase, uint64_t dmaAddressLimit) -> bool;
	static void setInterrupterEnabled(const MappedController &controller, bool enabled);
	static void acknowledgeEvents(const MappedController &controller);
	static void updateEventDequeuePointer(const MappedController &controller);
	static auto eventType(const XhciTrb &event) -> uint32_t;
	static auto completionCode(const XhciTrb &event) -> uint32_t;
	static void ringDoorbell(const MappedController &controller, uint32_t target, uint32_t value);
	static auto enqueueCommand(MappedController &controller, const XhciTrb &command) -> bool;
	static auto submitNoopCommand(MappedController &controller) -> bool;
	static auto commandPhys(const MappedController &controller, uint32_t index) -> uint64_t;
	static auto enqueueCommandAndGetPhys(MappedController &controller, const XhciTrb &command, uint64_t &phys) -> bool;
	static auto waitForCommandCompletion(MappedController &controller, uint64_t commandTrbPhys, XhciTrb &completion, int timeoutMs = 1000) -> bool;
	static auto runCommand(MappedController &controller, const XhciTrb &command, XhciTrb &completion, int timeoutMs = 1000) -> bool;
	static auto drainEvents(MappedController &controller, uint32_t &loggedEvents) -> uint32_t;
	static void logControllerStatus(const MappedController &controller, const char *phase);
	static void logPorts(const MappedController &controller);
	static void logPortState(const MappedController &controller, uint8_t port, const char *phase);
	static void logDeviceContext(const MappedController &controller, const XhciDevice &device, const char *phase);
	static void postStartProbe(MappedController &controller);
	static auto contextSize(const MappedController &controller) -> uint32_t;
	static auto contextPtr(const AllocatedPage &page, const MappedController &controller, uint32_t index) -> uint32_t *;
	static auto contextDword(const AllocatedPage &page, const MappedController &controller, uint32_t index, uint32_t dword) -> uint32_t;
	static void setContextDword(const AllocatedPage &page, const MappedController &controller, uint32_t index, uint32_t dword, uint32_t value);
	static auto endpointState(const MappedController &controller, const XhciDevice &device, uint8_t endpointId) -> uint32_t;
	static auto portSpeed(const MappedController &controller, uint8_t port) -> uint8_t;
	static auto ep0MaxPacketForSpeed(const MappedController &controller, uint8_t rootPort, uint8_t speed) -> uint16_t;
	static auto endpointInterval(uint8_t speed, uint8_t attributes, uint8_t interval) -> uint8_t;
	static auto usbEndpointId(uint8_t endpointAddress) -> uint8_t;
	static auto xhciEndpointType(uint8_t endpointAddress, uint8_t attributes) -> uint8_t;
	static auto contextIndexForEndpointId(uint8_t endpointId) -> uint32_t;
	static auto waitForTransferEvent(MappedController &controller, uint8_t slotId, uint8_t endpointId, XhciTrb &completion, int timeoutMs = 1000, bool logTimeoutPath = true, uint64_t expectedTrbPhys = 0) -> bool;
	static auto enqueueTransferTrb(XhciDevice &device, const XhciTrb &trb) -> bool;
	static auto enqueueEndpointTrb(UsbEndpoint &endpoint, const XhciTrb &trb) -> bool;
	static void recoverEndpoint(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint);
	static auto bulkOrInterruptTransfer(MappedController &controller, const XhciDevice &device, UsbEndpoint &endpoint, const uint64_t *pagePhysArray, uint32_t pageCount, uint32_t length, bool in, uint32_t *actualLength, int timeoutMs = 5000, bool logTimeout = true) -> bool;
	static auto controlTransferIn(MappedController &controller, XhciDevice &device, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index, uint16_t length, uint64_t dataPhys) -> bool;
	static auto controlTransferNoData(MappedController &controller, XhciDevice &device, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index) -> bool;
	static auto waitForPortReset(const MappedController &controller, uint8_t port, bool warmReset) -> bool;
	static auto resetPortIfNeeded(const MappedController &controller, uint8_t port) -> bool;
	static auto enableSlot(MappedController &controller, uint8_t &slotId) -> bool;
	static auto setupTransferRing(const MappedController &controller, XhciDevice &device) -> bool;
	static auto setupEndpointTransferRing(const MappedController &controller, UsbEndpoint &endpoint) -> bool;
	static void resetEndpointTransferRing(UsbEndpoint &endpoint);
	static auto setupDeviceContexts(const MappedController &controller, XhciDevice &device) -> bool;
	static auto addressDevice(MappedController &controller, const XhciDevice &device) -> bool;
	static auto disableSlot(MappedController &controller, const XhciDevice &device) -> bool;
	static auto stopEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool;
	static auto resetEndpoint(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool;
	static auto setEndpointDequeuePointer(MappedController &controller, const XhciDevice &device, const UsbEndpoint &endpoint) -> bool;
	static void resetControlTransferRing(XhciDevice &device);
	static void recoverControlEndpoint(MappedController &controller, XhciDevice &device);
	static void releaseDeviceMemory(const MappedController &controller, XhciDevice &device);
	static auto evaluateEp0Context(MappedController &controller, const XhciDevice &device) -> bool;
	static auto configureEndpoints(MappedController &controller, XhciDevice &device) -> bool;
	static auto readDeviceDescriptor(MappedController &controller, XhciDevice &device, uint16_t length) -> bool;
	static auto readStringDescriptor(MappedController &controller, XhciDevice &device, uint8_t index) -> std::string;
	static void parseConfigurationDescriptor(XhciDevice &device, const uint8_t *desc, uint16_t totalLength);
	static auto readConfigurationDescriptor(MappedController &controller, XhciDevice &device, uint8_t &configurationValue) -> bool;
	static auto setConfiguration(MappedController &controller, XhciDevice &device, uint8_t configurationValue) -> bool;
	static void bindClassDrivers(MappedController &controller, XhciDevice &device, uint32_t controllerId);
	static void retryMassStorageBindings(MappedController &controller);
	static auto prepareHubMetadata(MappedController &controller, XhciDevice &device) -> bool;
	static void submitHubInterruptTransfer(MappedController &controller, XhciDevice &hub);
	static void submitHidInterruptTransfer(MappedController &controller, XhciDevice &device, HidInterruptPipe &pipe);
	static auto enumerateDevice(MappedController &controller, uint32_t controllerId, uint8_t rootPort, uint32_t routeString, uint8_t depth, uint8_t speed, uint8_t parentSlotId, uint8_t hubPort, const char *location) -> bool;
	static auto hubPortStatus(MappedController &controller, XhciDevice &hub, uint8_t port, uint16_t &status, uint16_t &change) -> bool;
	static auto hubPortSpeed(const XhciDevice &hub, uint16_t status) -> uint8_t;
	static void clearHubPortChangeBits(MappedController &controller, XhciDevice &hub, uint8_t port, uint16_t change);
	static auto resetHubPort(MappedController &controller, XhciDevice &hub, uint8_t port, uint16_t &status) -> bool;
	static auto readHubDescriptor(MappedController &controller, XhciDevice &device, const UsbInterface &interface) -> bool;
	static void probeHub(MappedController &controller, XhciDevice &device, const UsbInterface &interface);
	static void configureBootHid(MappedController &controller, XhciDevice &device, const UsbInterface &interface);
	static void enumerateRootPorts(MappedController &controller, uint32_t controllerId);
	static void removeDevicesForRootPort(MappedController &controller, uint8_t rootPort);
	static void removeDevicesForHubPort(MappedController &controller, uint8_t parentSlotId, uint8_t hubPort);
	static auto findDeviceBySlot(MappedController &controller, uint8_t slotId) -> XhciDevice *;
	static auto hubHasChildOnPort(const MappedController &controller, uint8_t parentSlotId, uint8_t hubPort) -> bool;
	static void handleHubPortChange(MappedController &controller, uint8_t hubSlotId, uint8_t port);
	static void handleRootPortChange(MappedController &controller, uint32_t port);
	static auto findEndpointByAddress(XhciDevice &device, uint8_t endpointAddress) -> UsbEndpoint *;
	static void handleHubInterruptTransfer(MappedController &controller, const XhciTrb &event);
	static void handleHidInterruptTransfer(MappedController &controller, const XhciTrb &event);
	static auto handleAsyncInterruptTransfer(MappedController &controller, const XhciTrb &event) -> bool;
	static void drainPendingInterruptEvents(MappedController &controller);
	static void pollHubChanges(MappedController &controller);
	static void pollRootPortChanges(MappedController &controller);
	static auto startEventIrqHandler(MappedController &controller) -> bool;
	static auto setupInterrupts(MappedController &controller) -> bool;
	static auto setupControllerMemory(MappedController &controller, uint32_t maxScratchpads) -> bool;
	static auto bringUpController(MappedController &controller, size_t index) -> bool;
	static void startStorageHandlers();
};

#endif
