// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Evalyn Goemer & EvalynOS Contributors

#include "hal/Serial.hpp"

#include "hal/IOPort.hpp"
#include "utils/MMIO.hpp"

namespace kernel::common::hal {
	using namespace utils;

	SerialCtx Serial::earlyconSerial {};
	volatile u8 Serial::serialBufferIndex {};
	volatile char Serial::serialBuffer[256] {};

	namespace {
		void serialWriteReg(const SerialCtx *ctx, const u8 reg, u8 data) {
#ifdef __x86_64__
			if (ctx->portIO) {
				x86_64::hal::IOPort::outd8(ctx->addr + reg, data);
			} else {
				MMIO::write(ctx->addr + reg, data, sizeof(u8));
			}
#else
			assert(!ctx->portIO);
			mmio_write_8(ctx->addr + reg, data);
#endif
		}

		auto serialReadReg(const SerialCtx *ctx, u8 reg) -> u8 {
#ifdef __x86_64__
			if (ctx->portIO) {
				return x86_64::hal::IOPort::ind8(ctx->addr + reg);
			}

			return MMIO::read(ctx->addr + reg, sizeof(u8));
#else
			assert(!ctx->portIO);
			return mmio_read_8(ctx->addr + reg);
#endif
		}

		void serialSetDlab(const SerialCtx *ctx, const bool setting) {
			const u8 lcr = serialReadReg(ctx, SERIAL_LINE_CONF);

			if (setting) {
				serialWriteReg(ctx, SERIAL_LINE_CONF, lcr | SERIAL_DLAB_BIT);
			} else {
				serialWriteReg(ctx, SERIAL_LINE_CONF, lcr & ~SERIAL_DLAB_BIT);
			}
		}

		void serialSetDivisor(const SerialCtx *ctx, u16 divsor) {
			serialSetDlab(ctx, true);
			serialWriteReg(ctx, SERIAL_DLAB_DIV_LO, divsor & 0xff);
			serialWriteReg(ctx, SERIAL_DLAB_DIV_HI, (divsor >> 8) & 0xff);
			serialSetDlab(ctx, false);
		}

		void serialSetInterrupts(const SerialCtx *ctx, u8 setting) {
			serialSetDlab(ctx, false);
			serialWriteReg(ctx, SERIAL_INTR_CONF, setting);
		}

		void serialSetMcr(const SerialCtx *ctx, u8 setting) {
			serialWriteReg(ctx, SERIAL_MODEM_CONF, setting);
		}

		void serialSetLcr(const SerialCtx *ctx, u8 lcr) {
			serialWriteReg(ctx, SERIAL_LINE_CONF, lcr);
		}

		void serialSetFifo(const SerialCtx *ctx, u8 fifo) {
			serialWriteReg(ctx, SERIAL_FIFO_CONF, fifo);
		}

		int serialTransmitEmpty(const SerialCtx *ctx) {
			return serialReadReg(ctx,  SERIAL_LINE_INFO) & SERIAL_TX_EMPTY_BIT;
		}

		int serialDataReady(const SerialCtx *ctx) {
			return serialReadReg(ctx,  SERIAL_LINE_INFO) & SERIAL_DATA_READY_BIT;
		}

	}

	auto Serial::serialTest(const SerialCtx * ctx) -> int {
		serialSetDivisor(ctx, SERIAL_115200_BAUD);
		serialSetLcr(ctx, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
		serialSetFifo(ctx, SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
		serialSetMcr(ctx, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_LOOP_ENABLE);
		serialSetDlab(ctx, false);

		for (int i = 0; i < SERIAL_TEST_RETRIES; i++) {
			serialWriteReg(ctx, SERIAL_TX_BUFF, SERIAL_TEST_MAGIC);

			for(int j = 0; j < 4096; j++) {
				//asm volatile("pause");
				x86_64::hal::IOPort::ioWait();
			}

			if (serialReadReg(ctx, SERIAL_RX_BUFF) == SERIAL_TEST_MAGIC) {
				return 0;
			}
		}

		// fallback test to just make sure it exists at all
		serialWriteReg(ctx, SERIAL_SCRATCH_REG, SERIAL_TEST_MAGIC);

		if (serialReadReg(ctx, SERIAL_SCRATCH_REG) == SERIAL_TEST_MAGIC) {
			return -1;
		}

		return -2;
	}

	auto Serial::setupEarlySerial(const u64 addr, const bool portIO) -> int {
		earlyconSerial.addr = addr;
		earlyconSerial.portIO = portIO;

		serialSetInterrupts(&earlyconSerial, false);

		const int status = serialTest(&earlyconSerial);

		if(status == -2) {
			return status;
		}

		serialSetDivisor(&earlyconSerial, SERIAL_115200_BAUD);
		serialSetLcr(&earlyconSerial, SERIAL_LCR_8BIT | SERIAL_LCR_1STOP | SERIAL_LCR_PARITY_NONE);
		serialSetFifo(&earlyconSerial, SERIAL_FIFO_ENABLE | SERIAL_FIFO_THRESH_1b | SERIAL_FIFO_TX_FLUSH | SERIAL_FIFO_RX_FLUSH);
		serialSetMcr(&earlyconSerial, SERIAL_MCR_TX_ENABLE | SERIAL_MCR_RX_ENABLE | SERIAL_MCR_IRQ_ENABLE);
		serialSetDlab(&earlyconSerial, false);
		earlyconSerial.working = true;

		return status;

	}

	auto Serial::detectEarlySerial(const u64 *addrOut, const bool *portIOOut) -> bool {
		(void) addrOut;
		(void) portIOOut;

		/*struct SPCR* spcr = acpi_find_sdt(ACPI_SPCR_TABLE_SIGNATURE);
		if (!spcr)
			goto try_dbg2;
		if (!spcr->address.address)
			goto try_dbg2;
		if (spcr->interface_type != SPCR_IFACE_16550)
			goto try_dbg2;
		#ifndef __x86_64__
		if (spcr->address.address_space_id != ACPI_ADDRESS_TYPE_MMIO)
			goto try_dbg2;
		#else
		if (spcr->address.address_space_id != ACPI_ADDRESS_TYPE_MMIO && spcr->address.address_space_id != ACPI_ADDRESS_TYPE_PORT_IO)
			goto try_dbg2;
		#endif

		if (addr_out) *addr_out = spcr->address.address;
		if (portIO_out) *portIO_out = (spcr->address.address_space_id == ACPI_ADDRESS_TYPE_PORT_IO);*/
		//return true;

		// TODO: parse the DBG2 table
		//try_dbg2:
		return false;

	}

	auto Serial::serialRead(SerialCtx *ctx) -> int {
		if (!ctx->working) {
			return -1;
		}

		if (not serialDataReady(ctx)) {
			return -1;
		}

		return serialReadReg(ctx,  SERIAL_RX_BUFF);

	}

	void Serial::serialSend(SerialCtx* ctx, char c) {
		if (!ctx->working) {
			return;
		}

		for (int i = 0; i < 100000; i++) {
			if (serialTransmitEmpty(ctx)) {
				break;
			}

			asm volatile("pause");
		}

		serialWriteReg(ctx, SERIAL_TX_BUFF, c);
	}
}