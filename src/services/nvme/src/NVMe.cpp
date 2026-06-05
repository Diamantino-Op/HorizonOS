#include "NVMe.hpp"

#include "bits/linux/linux_sched.h"
#include "horizonos/generic.h"
#include "sys/mman.h"
#include "unistd.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
	auto nvmeTrimString(const char* inStr, const size_t len) noexcept -> string {
		const string str(inStr, len);
		const auto end = str.find_last_not_of(' ');

		return end == string::npos ? string{} : str.substr(0, end + 1);
	}
}

auto NvmeDriver::coreHandler(void *ctx) -> void * {
	const auto *coreStruct = static_cast<CoreStruct *>(ctx);

	printf("NVMe: Core handler started for core %lu with ID %ld", coreStruct->coreSlot, coreStruct->cpuId);
	fflush(stdout);

    lockToCore(coreStruct->cpuId);

	printf("NVMe: Core %lu locked to CPU ID %ld. (From %d)", coreStruct->coreSlot, coreStruct->cpuId, sched_getcpu());
	fflush(stdout);

    const uint64_t readType  = NVME_READ_MSG_BASE  + coreStruct->cpuId;
    const uint64_t writeType = NVME_WRITE_MSG_BASE + coreStruct->cpuId;
    const uint64_t flushType = NVME_FLUSH_MSG_BASE + coreStruct->cpuId;

    auto filterOpts = filter_options();
    filterOpts.whiteListTypes = new uint64_t[3]{ readType, writeType, flushType };
    filterOpts.whiteListCount = 3;

    // Use the largest possible message struct as the receive buffer
    alignas(8) uint8_t msgBuf[sizeof(NvmeWriteMsgData)] {};

    for (;;) {
        memset(msgBuf, 0, sizeof(msgBuf));

        auto recvMsg   = hos_msg();
        recvMsg.buffer = msgBuf;
        recvMsg.length = sizeof(msgBuf);

        if (receive_horizonos_message(coreStruct->nvmePort, &recvMsg, &filterOpts) != 0) {
        	continue;
        }

        if (recvMsg.type == readType) {
            const auto* req = reinterpret_cast<NvmeReadMsgData*>(msgBuf);

            NvmeReadReplyMsgData reply {};
            reply.pageCount = req->pageCount;

            if (req->controllerId < coreStruct->controllerDrivers->size()) {
                reply.success = (*coreStruct->controllerDrivers)[req->controllerId].read(req->nsid, req->lba, req->pagePhysArray, req->pageCount, coreStruct->coreSlot);
            }

            auto replyMsg   = hos_msg();
            replyMsg.type   = NVME_REPLY_READ_MSG_BASE + coreStruct->cpuId;
            replyMsg.port   = coreStruct->nvmePort;
            replyMsg.buffer = &reply;
            replyMsg.length = sizeof(reply);

            send_horizonos_message(coreStruct->nvmePort, recvMsg.port, &replyMsg);
        } else if (recvMsg.type == writeType) {
            const auto* req = reinterpret_cast<NvmeWriteMsgData*>(msgBuf);

            NvmeWriteReplyMsgData reply {};

            if (req->controllerId < coreStruct->controllerDrivers->size()) {
                reply.success = (*coreStruct->controllerDrivers)[req->controllerId].write(req->nsid, req->lba, req->pagePhysArray, req->pageCount, coreStruct->coreSlot);
            }

            auto replyMsg   = hos_msg();
            replyMsg.type   = NVME_REPLY_WRITE_MSG_BASE + coreStruct->cpuId;
            replyMsg.port   = coreStruct->nvmePort;
            replyMsg.buffer = &reply;
            replyMsg.length = sizeof(reply);

            send_horizonos_message(coreStruct->nvmePort, recvMsg.port, &replyMsg);
        } else if (recvMsg.type == flushType) {
            const auto* req = reinterpret_cast<NvmeFlushMsgData*>(msgBuf);

            NvmeFlushReplyMsgData reply {};

            if (req->controllerId < coreStruct->controllerDrivers->size()) {
                reply.success = (*coreStruct->controllerDrivers)[req->controllerId].flush(req->nsid, coreStruct->coreSlot);
            }

            auto replyMsg   = hos_msg();
            replyMsg.type   = NVME_REPLY_FLUSH_MSG_BASE + coreStruct->cpuId;
            replyMsg.port   = coreStruct->nvmePort;
            replyMsg.buffer = &reply;
            replyMsg.length = sizeof(reply);

            send_horizonos_message(coreStruct->nvmePort, recvMsg.port, &replyMsg);
        }
    }
}

void NvmeDriver::attachRegisters(const uint64_t physData, const uint64_t virtData, uint64_t *base, const uint64_t size, PciDevice *ownDevice) noexcept {
	this->dataPhys = physData;
	this->dataVirt = virtData;
	this->mmioBase = base;
	this->mmioSize = size;
	this->device = ownDevice;
}

auto NvmeDriver::resetController() const noexcept -> bool {
	if (this->mmioBase == nullptr) {
		return false;
	}

	uint32_t controller = mmioRead32(this->mmioBase, 0x14); // CC
	controller &= ~(1U << 0); // EN = 0

	mmioWrite32(this->mmioBase, 0x14, controller);

	for (int i = 0; i < 100000; ++i) {
		const uint32_t csts = mmioRead32(this->mmioBase, 0x1C); // CSTS

		if ((csts & 0x1U) == 0) {
			return true;
		}

		usleep(10000);
	}

	return false;
}

auto NvmeDriver::enableController() const noexcept -> bool {
	if (this->mmioBase == nullptr) {
		return false;
	}

	uint32_t controller = mmioRead32(this->mmioBase, 0x14); // CC
	controller |= (1U << 0);      // EN
	controller &= ~(0xFU << 16);  // clear IOSQES
	controller &= ~(0xFU << 20);  // clear IOCQES
	controller |= (6U << 16);     // IOSQES = 6 => 64-byte SQ entries
	controller |= (4U << 20);     // IOCQES = 4 => 16-byte CQ entries

	mmioWrite32(this->mmioBase, 0x14, controller);

	for (int i = 0; i < 100000; ++i) {
		const uint32_t csts = mmioRead32(this->mmioBase, 0x1C); // CSTS

		if ((csts & 0x1U) != 0) {
			return true;
		}

		usleep(10000);
	}

	return false;
}

auto NvmeDriver::initializeAdminQueues() noexcept -> bool {
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

auto NvmeDriver::submitAdminCommand(const Command &command, CompletionEntry &result) noexcept -> bool {
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

		usleep(10000);
	}

	return false;
}

auto NvmeDriver::identifyController() noexcept -> bool {
	memset(reinterpret_cast<void*>(this->dataVirt), 0, 0x1000);

	Command cmd {};
	cmd.cdw0.setOpCode(0x06);           // Identify opcode
	cmd.nsid      = 0;                       // nsid = 0 for controller identify
	cmd.dptrLow   = this->dataPhys;                // PRP Entry 1: 4 KiB buffer
	cmd.dptrHigh  = 0;
	cmd.cdw10.raw = 0x01;                    // CNS = 01h → Identify Controller

	CompletionEntry cqe {};

	if (not submitAdminCommand(cmd, cqe)) {
		return false;
	}

	memcpy(&controllerInfo, reinterpret_cast<void*>(this->dataVirt), 0x1000);

	printf("NVMe: Controller model: %s, serial: %s, namespaces: %u", nvmeTrimString(controllerInfo.mn, sizeof(controllerInfo.mn)).c_str(), nvmeTrimString(controllerInfo.sn, sizeof(controllerInfo.sn)).c_str(), controllerInfo.nn);
	fflush(stdout);

	return true;
}

auto NvmeDriver::identifyNamespace(const uint32_t namespaceId) noexcept -> bool {
	memset(reinterpret_cast<void*>(this->dataVirt), 0, 0x1000);

	Command cmd {};
	cmd.cdw0.setOpCode(0x06);       // Identify opcode
	cmd.nsid      = namespaceId;         // Target namespace
	cmd.dptrLow   = this->dataPhys;
	cmd.dptrHigh  = 0;
	cmd.cdw10.raw = 0x00;               // CNS = 00h → Identify Namespace

	CompletionEntry cqe {};

	if (not submitAdminCommand(cmd, cqe)) {
		return false;
	}

	const auto* nsData = reinterpret_cast<IdentifyNamespaceData*>(this->dataVirt);

	const uint8_t lbafIndex = nsData->flbas & 0x0FU;
	const uint8_t lbads     = nsData->lbaf[lbafIndex].lbads;

	NamespaceInfo info {};

	info.nsid      = namespaceId;
	info.totalLbas = nsData->nsze;
	info.lbaSize   = (lbads > 0) ? (1U << lbads) : 512U;
	info.valid     = true;

	printf("NVMe: Namespace %u — %lu LBAs × %u bytes = %lu MB", namespaceId, info.totalLbas, info.lbaSize, (info.totalLbas * info.lbaSize) / (static_cast<uint64_t>(1024U * 1024U)));
	fflush(stdout);

	namespaces.push_back(info);

	return true;
}

auto NvmeDriver::read(const uint32_t namespaceId, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount, const uint64_t coreSlot) noexcept -> bool {
    if (coreSlot >= ioQueues.size() or not ioQueues[coreSlot].valid) {
    	return false;
    }

    if (pagePhysArray == nullptr or pageCount == 0) {
    	return false;
    }

    const NamespaceInfo* nsInfo = nullptr;

    for (const auto& currNs : namespaces) {
        if (currNs.nsid == namespaceId && currNs.valid) {
	        nsInfo = &currNs; break;
        }
    }

    if (nsInfo == nullptr) {
    	return false;
    }

    const uint64_t blockCount = (static_cast<uint64_t>(pageCount) * 0x1000) / nsInfo->lbaSize;

    uint64_t prp1 = 0;
    uint64_t prp2 = 0;
    vector<PrpListPage> prpListPages;

    if (!buildChainedPrpList(pagePhysArray, pageCount, prp1, prp2, prpListPages)) {
        return false;
    }

    Command cmd {};
    cmd.cdw0.setOpCode(0x02); // Read
    cmd.nsid      = namespaceId;
    cmd.dptrLow   = prp1;
    cmd.dptrHigh  = prp2;
    cmd.cdw10.raw = static_cast<uint32_t>(lba & 0xFFFFFFFFU);
    cmd.cdw11.raw = static_cast<uint32_t>((lba >> 32) & 0xFFFFFFFFU);
    cmd.cdw12.raw = static_cast<uint32_t>((blockCount - 1) & 0xFFFFU);

    CompletionEntry cqe {};
    const bool isOk = submitIoCommand(ioQueues[coreSlot], cmd, cqe);

    // PRP list pages are only needed during command execution; free them now
    freePrpListPages(prpListPages);

    return isOk;
}

auto NvmeDriver::write(const uint32_t namespaceId, const uint64_t lba, const uint64_t *pagePhysArray, const uint32_t pageCount, const uint64_t coreSlot) noexcept -> bool {
	if (coreSlot >= ioQueues.size() or not ioQueues[coreSlot].valid) {
		return false;
	}

	if (pagePhysArray == nullptr or pageCount == 0) {
		return false;
	}

	const NamespaceInfo* nsInfo = nullptr;

	for (const auto& currNs : namespaces) {
		if (currNs.nsid == namespaceId && currNs.valid) {
			nsInfo = &currNs; break;
		}
	}

	if (nsInfo == nullptr) {
		return false;
	}

	const uint64_t blockCount = (static_cast<uint64_t>(pageCount) * 0x1000) / nsInfo->lbaSize;

	uint64_t prp1 = 0;
	uint64_t prp2 = 0;
	vector<PrpListPage> prpListPages;

	if (not buildChainedPrpList(pagePhysArray, pageCount, prp1, prp2, prpListPages)) {
		return false;
	}

	Command cmd {};
	cmd.cdw0.setOpCode(0x01); // Write
	cmd.nsid      = namespaceId;
	cmd.dptrLow   = prp1;
	cmd.dptrHigh  = prp2;
	cmd.cdw10.raw = static_cast<uint32_t>(lba & 0xFFFFFFFFU);
	cmd.cdw11.raw = static_cast<uint32_t>((lba >> 32) & 0xFFFFFFFFU);
	cmd.cdw12.raw = static_cast<uint32_t>((blockCount - 1) & 0xFFFFU);

	CompletionEntry cqe {};
	const bool isOk = submitIoCommand(ioQueues[coreSlot], cmd, cqe);

	freePrpListPages(prpListPages);

	return isOk;
}

auto NvmeDriver::flush(const uint32_t namespaceId, const uint64_t coreSlot) noexcept -> bool {
	if (coreSlot >= ioQueues.size() || !ioQueues[coreSlot].valid) {
		return false;
	}

	Command cmd {};
	cmd.cdw0.setOpCode(0x00);
	cmd.nsid = namespaceId;

	CompletionEntry cqe {};
	return submitIoCommand(ioQueues[coreSlot], cmd, cqe);
}

auto NvmeDriver::buildChainedPrpList(const uint64_t *pagePhysArray, const uint32_t pageCount, uint64_t &prp1, uint64_t &prp2, vector<PrpListPage> &prpListPages) noexcept -> bool {
	prpListPages.clear();

    if (pageCount == 0) {
    	return false;
    }

    // PRP1 is always the first data page
    prp1 = pagePhysArray[0];
    prp2 = 0;

    if (pageCount == 1) {
    	return true;
    }

    // Two pages: PRP2 is a direct pointer, no list needed
    if (pageCount == 2) {
        prp2 = pagePhysArray[1];

        return true;
    }

    // Three or more pages: PRP2 points to the first list page.
    // Each list page holds 512 uint64_t entries.
    // When a list page is full, its last entry points to the next list page
    // instead of a data page (chaining). The final list page uses all 512
    // entries for data pages.
    //
    // Data pages to place into lists: pagePhysArray[1..pageCount-1]
    const uint32_t dataInList = pageCount - 1;  // page[0] is already in PRP1

    // Calculate how many list pages we need.
    // Each non-final list page contributes 511 data entries (slot 512 = next list ptr).
    // Final list page contributes up to 512 data entries.
    // So: listPageCount = ceil(dataInList / 511) but last page can hold 512.
    // Simpler: allocate greedily, fill 511 per page until <= 512 remain.

    uint32_t remaining   = dataInList;
    uint32_t dataOffset  = 1;  // index into pagePhysArray (start after PRP1)

    const PrpListPage* prevPage = nullptr;

    while (remaining > 0) {
        PrpListPage listPage {};

        if (allocPhysPage(&listPage.phys) != 0) {
        	return false;
        }

        uint64_t virtAddr = 0;

        if (mmap_phys(listPage.phys, 0x1000, &virtAddr, false) != 0) {
        	return false;
        }

        listPage.virt = virtAddr;

        memset(reinterpret_cast<void*>(listPage.virt), 0, 0x1000);
        prpListPages.push_back(listPage);

        auto* slots = reinterpret_cast<uint64_t*>(listPage.virt);

        // Decide how many data entries this page will hold.
        // If this is the last list page (remaining <= 512), fill all with data.
        // Otherwise fill 511, leaving slot[511] for the next list page pointer.
        const bool isLastListPage = (remaining <= 512);
        const uint32_t dataSlots = isLastListPage ? remaining : 511;

        for (uint32_t i = 0; i < dataSlots; ++i) {
            slots[i] = pagePhysArray[dataOffset++];
        }

        remaining -= dataSlots;

        // If there is a previous list page, write this page's physical address
        // into its last slot (chaining)
        if (prevPage != nullptr) {
            auto* prevSlots = reinterpret_cast<uint64_t*>(prevPage->virt);

            prevSlots[511]  = listPage.phys;
        }

        prevPage = &prpListPages.back();
    }

    // PRP2 points to the first list page
    prp2 = prpListPages.front().phys;

    return true;
}

void NvmeDriver::freePrpListPages(vector<PrpListPage> &pages) noexcept {
	for (const auto &[_, virt] : pages) {
		if (virt != 0) {
			munmap(reinterpret_cast<void*>(virt), 0x1000);
		}
	}

	pages.clear();
}

auto NvmeDriver::createIoQueueForCore(const uint64_t coreSlot, const uint16_t queueId) noexcept -> bool {
    IoQueuePair pair {};
    pair.queueId = queueId;
    pair.depth   = 64;

    // Allocate CQ
    uint64_t cqVirt = 0;

    if (allocPhysPage(&pair.cqPhys) != 0) {
    	return false;
    }

    if (mmap_phys(pair.cqPhys, pair.depth * sizeof(CompletionEntry), &cqVirt, false) != 0) {
    	return false;
    }

    memset(reinterpret_cast<void*>(cqVirt), 0, pair.depth * sizeof(CompletionEntry));
    pair.cq = reinterpret_cast<CompletionEntry*>(cqVirt);

    // Allocate SQ
    uint64_t sqVirt = 0;

    if (allocPhysPage(&pair.sqPhys) != 0) {
    	return false;
    }

    if (mmap_phys(pair.sqPhys, pair.depth * sizeof(Command), &sqVirt, false) != 0) {
    	return false;
    }

    memset(reinterpret_cast<void*>(sqVirt), 0, pair.depth * sizeof(Command));
    pair.sq = reinterpret_cast<Command*>(sqVirt);

    // Admin command: Create I/O Completion Queue (opcode 0x05)
    Command cqCmd {};
    cqCmd.cdw0.setOpCode(0x05);
    cqCmd.dptrLow   = pair.cqPhys;
    cqCmd.dptrHigh  = 0;
    cqCmd.cdw10.raw = (queueId & 0xFFFFU) | (((pair.depth - 1) & 0xFFFFU) << 16);
    cqCmd.cdw11.raw = 0x1U;  // PC=1, no interrupts

    CompletionEntry cqe {};

    if (!submitAdminCommand(cqCmd, cqe)) {
    	return false;
    }

    // Admin command: Create I/O Submission Queue (opcode 0x01)
    Command sqCmd {};
    sqCmd.cdw0.setOpCode(0x01);
    sqCmd.dptrLow   = pair.sqPhys;
    sqCmd.dptrHigh  = 0;
    sqCmd.cdw10.raw = (queueId & 0xFFFFU) | (((pair.depth - 1) & 0xFFFFU) << 16);
    sqCmd.cdw11.raw = 0x1U | ((queueId & 0xFFFFU) << 16);  // PC=1, CQID=queueId

    if (!submitAdminCommand(sqCmd, cqe)) {
    	return false;
    }

    pair.valid = true;

    // Grow the vector to fit coreSlot if needed, then assign
    if (coreSlot >= ioQueues.size()) {
        ioQueues.resize(coreSlot + 1);
    }

    ioQueues[coreSlot] = pair;

    if (maxTransferBlocks == 0) {
        maxTransferBlocks = (controllerInfo.mdts > 0) ? (1U << controllerInfo.mdts) : 32;
    }

    printf("NVMe: Created I/O queue %u for core slot %lu", queueId, coreSlot);
    fflush(stdout);

    return true;
}

auto NvmeDriver::submitIoCommand(IoQueuePair &queue, const Command& command, CompletionEntry &result) const noexcept -> bool {
	if (queue.sq == nullptr or queue.cq == nullptr or not queue.valid) {
		return false;
	}

	queue.sq[queue.sqTail] = command;
	queue.sqTail = (queue.sqTail + 1) % queue.depth;

	// SQ Tail doorbell: 0x1000 + (2 * qid) * stride
	mmioWrite32(mmioBase, 0x1000 + ((2U * queue.queueId) * doorbellStride), queue.sqTail);

	for (int i = 0; i < 100000; ++i) {
		const CompletionEntry& cqe = queue.cq[queue.cqHead];

		if ((cqe.status.phase & 0x1U) == queue.cqPhase) {
			result = cqe;
			queue.cqHead = (queue.cqHead + 1) % queue.depth;

			if (queue.cqHead == 0) {
				queue.cqPhase ^= 1U;
			}

			// CQ Head doorbell: 0x1000 + (2 * qid + 1) * stride
			mmioWrite32(mmioBase, 0x1000 + (((2U * queue.queueId) + 1U) * doorbellStride), queue.cqHead);

			return (cqe.status.statusCode == 0);
		}

		usleep(10);
	}
	return false;
}

void NvmeDriver::shutdown() noexcept {
	if (mmioBase == nullptr) {
		return;
	}

    // ── Step 1: Delete all I/O queues (one pair per core slot) ──────────────
    // Must delete SQ before CQ for each queue pair (spec requirement)
    for (auto &queue : ioQueues) {
        if (not queue.valid) {
        	continue;
        }

        // Delete I/O Submission Queue (opcode 0x00 in the Delete SQ command)
        Command delSQ {};
        delSQ.cdw0.setOpCode(0x00);        // Delete I/O SQ
        delSQ.cdw10.raw = queue.queueId;       // QID to delete

        CompletionEntry cqe {};
        submitAdminCommand(delSQ, cqe);    // best-effort, ignore return value

        // Delete I/O Completion Queue (opcode 0x04)
        Command delCQ {};
        delCQ.cdw0.setOpCode(0x04);        // Delete I/O CQ
        delCQ.cdw10.raw = queue.queueId;

        submitAdminCommand(delCQ, cqe);

        // Unmap the queue memory
        if (queue.sq != nullptr) {
        	munmap(queue.sq, queue.depth * sizeof(Command));
        }

        if (queue.cq != nullptr) {
        	munmap(queue.cq, queue.depth * sizeof(CompletionEntry));
        }

        queue = IoQueuePair {};
    }

    ioQueues.clear();

    // ── Step 2: Signal normal shutdown via CC.SHN = 01b ─────────────────────
    // CC register is at offset 0x14
    // SHN field is bits [15:14]
    uint32_t controller = mmioRead32(mmioBase, 0x14);

    controller &= ~(0x3U << 14);   // clear SHN
    controller |=  (0x1U << 14);   // SHN = 01b → Normal Shutdown

    mmioWrite32(mmioBase, 0x14, controller);

    // Poll CSTS.SHST (bits [3:2]) until it reads 10b (shutdown complete)
    // Spec says to wait up to ~500 ms; we allow 50 000 × 10 µs = 500 ms
    for (int i = 0; i < 50000; ++i) {
        const uint32_t csts = mmioRead32(mmioBase, 0x1C);
        const uint32_t shst = (csts >> 2) & 0x3U;

        if (shst == 0x2U) {
        	break;  // 10b = shutdown complete
        }

        usleep(10000);
    }

    // ── Step 3: Free admin queue memory ─────────────────────────────────────
    if (adminSQ != nullptr) {
        munmap(adminSQ, adminQDepth * sizeof(Command));

        adminSQ = nullptr;
    }

    if (adminCQ != nullptr) {
        munmap(adminCQ, adminQDepth * sizeof(CompletionEntry));

        adminCQ = nullptr;
    }

    // Free the shared identify buffer
    if (dataVirt != 0) {
        munmap(reinterpret_cast<uint64_t *>(dataVirt), 0x1000);

        dataVirt = 0;
    }

    namespaces.clear();

    mmioBase = nullptr;
    mmioSize = 0;
    device   = nullptr;

    adminSQTail  = 0;
    adminCQHead  = 0;
    adminCQPhase = 1;

    printf("NVMe: Controller shutdown complete");
    fflush(stdout);
}

auto NvmeDriver::getNamespaceCount() const noexcept -> uint32_t {
	return controllerInfo.nn;
}

auto NvmeDriver::getActiveNamespaces(vector<uint32_t> &nsIDs) noexcept -> bool {
	memset(reinterpret_cast<void*>(this->dataVirt), 0, 4096);

	Command cmd {};
	cmd.cdw0.setOpCode(0x06);  // Identify opcode
	cmd.nsid      = 0;          // Start after NSID 0 = return all active NSIDs
	cmd.dptrLow   = this->dataPhys;
	cmd.dptrHigh  = 0;
	cmd.cdw10.raw = 0x02;       // CNS = 02h → Active Namespace ID List

	CompletionEntry completionEntry {};

	if (not submitAdminCommand(cmd, completionEntry)) {
		return false;
	}

	// The response is an array of up to 1024 uint32_t NSIDs.
	// A value of 0x00000000 marks the end of the list.
	const auto* list = reinterpret_cast<const uint32_t*>(this->dataVirt);

	for (int i = 0; i < 1024; ++i) {
		if (list[i] == 0) {
			break;
		}

		nsIDs.push_back(list[i]);
	}

	return true;
}

auto pciRead32(const uint64_t nvmePort, const uint64_t pciPort, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset) -> uint32_t {
	auto sendMsg   = hos_msg();
	auto readData  = PciReadMsgData { .bus = bus, .dev = dev, .func = func, .offset = offset, .width = 32 };

	sendMsg.type   = PCI_READ_MSG_TYPE;
	sendMsg.port   = pciPort;
	sendMsg.buffer = &readData;
	sendMsg.length = sizeof(PciReadMsgData);

	send_horizonos_message(nvmePort, pciPort, &sendMsg);

	auto recvMsg   = hos_msg();
	auto replyData = PciReadReplyMsgData{};

	recvMsg.buffer = &replyData;
	recvMsg.length = sizeof(PciReadReplyMsgData);

	auto filter           = filter_options();

	filter.whiteListTypes = new uint64_t[1] { PCI_READ_REPLY_MSG_TYPE };
	filter.whiteListCount = 1;

	receive_horizonos_message(nvmePort, &recvMsg, &filter);

	delete[] filter.whiteListTypes;

	return replyData.data;
}

void pciWrite32(const uint64_t nvmePort, const uint64_t pciPort, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t offset, const uint32_t data) {
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