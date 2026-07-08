#ifndef HORIZONOS_UACPI_KERNEL_API_HPP
#define HORIZONOS_UACPI_KERNEL_API_HPP

#include "uacpi/kernel_api.h"

#include <cstdint>

constexpr uint64_t PCI_READ_MSG_TYPE = 0x20;
constexpr uint64_t PCI_READ_REPLY_MSG_TYPE = 0x30;
constexpr uint64_t PCI_WRITE_MSG_TYPE = 0x40;
constexpr uint64_t PCI_WRITE_REPLY_MSG_TYPE = 0x41;

constexpr uint64_t IRQ_RECEIVE_MSG_TYPE = 0x1000;

constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;

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

struct IrqReceiveData {
	uint64_t irqNum {};
	uint64_t cpuId {};
	bool isIrq {};
};

struct IrqHandleStruct {
	uacpi_interrupt_handler handler {};
	uacpi_handle ctx {};
};

struct UacpiIoRange {
	uacpi_io_addr base;
	uacpi_size len;
};

struct WorkItem {
	uacpi_work_handler handler;
	uacpi_handle ctx;
};

struct PciHandle {
	uacpi_pci_address address;
};

#endif
