#ifndef HORIZONOS_UACPI_SERVICE_HPP
#define HORIZONOS_UACPI_SERVICE_HPP

#include <cstddef>
#include <cstdint>

constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t CHECK_MSG_TYPE = 0x4;
constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t REPLY_CHECK_MSG_TYPE = 0x7;

constexpr uint64_t PCI_READY_MSG_TYPE = 0x10;

constexpr uint64_t MCFG_DONE_MSG_TYPE = 0x100;
constexpr uint64_t MCFG_SEGMENT_MSG_TYPE = 0x200;

constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;

struct RegisterMsgData {
	uint16_t ownerPid {};
	uint16_t tid {};
	char name[16] {};
	size_t nameLength {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct GetMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct CheckMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct RegisterReplyMsgData {
	bool success {};
};

struct CheckReplyMsgData {
	bool exists {};
};

struct GetReplyMsgData {
	uint64_t port {};
	uint16_t tid {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct McfgSegmentMsgData {
	uint64_t ecamBase {};
	uint64_t segment {};
	uint64_t bbn {};
	uint8_t endBus {};
};

class UacpiService {
public:
	auto start() -> int;
};

#endif
