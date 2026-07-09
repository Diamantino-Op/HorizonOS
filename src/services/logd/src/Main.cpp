#include <abi-bits/hos_msg.h>
#include <horizonos/generic.h>
#include <horizonos/syscall.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
	constexpr uint64_t REGISTER_MSG_TYPE = 0x1;
	constexpr uint64_t REPLY_REGISTER_MSG_TYPE = 0x5;
	constexpr uint64_t NAME_REGISTRY_PORT = 1;

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

	uint64_t logdPort = 0;

	void fillName(char *dst, const size_t dstSize, size_t &length, const char *name) {
		const size_t copyLen = std::min(dstSize - 1, strlen(name));
		memcpy(dst, name, copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	bool registerService(const char *name) {
		RegisterMsgData data {};
		data.ownerPid = static_cast<uint16_t>(getpid());
		data.tid = static_cast<uint16_t>(gettid());
		data.versionMinor = 1;
		fillName(data.name, sizeof(data.name), data.nameLength, name);

		hos_msg msg {};
		msg.type = REGISTER_MSG_TYPE;
		msg.port = NAME_REGISTRY_PORT;
		msg.buffer = &data;
		msg.length = sizeof(data);

		if (send_horizonos_message(logdPort, NAME_REGISTRY_PORT, &msg) != 0) {
			return false;
		}

		RegisterReplyMsgData reply {};
		hos_msg recv {};
		recv.buffer = &reply;
		recv.length = sizeof(reply);

		uint64_t type = REPLY_REGISTER_MSG_TYPE;
		filter_options filter {};
		filter.whiteListTypes = &type;
		filter.whiteListCount = 1;

		return receive_horizonos_message(logdPort, &recv, &filter) == 0 && reply.success;
	}

	int readKernelLogs(const uint64_t afterSequence, HorizonKernelLogEntry *entries, const uint64_t maxEntries, uint64_t *entriesRead) {
		long ret = 0;
		const int err = syscall(SYSCALL_READ_KERNEL_LOG, &ret, afterSequence, reinterpret_cast<uint64_t>(entries), maxEntries);

		if (entriesRead != nullptr) {
			*entriesRead = static_cast<uint64_t>(ret);
		}

		return err;
	}

	std::string formatEntry(const HorizonKernelLogEntry &entry) {
		char line[512] {};
		snprintf(line, sizeof(line), "[%llu ns] INFO %s: %s\n",
			static_cast<unsigned long long>(entry.timestampNs),
			entry.id,
			entry.msg);
		return line;
	}

	bool writeAll(const int fd, const char *data, size_t length) {
		while (length > 0) {
			const ssize_t written = write(fd, data, length);

			if (written <= 0) {
				return false;
			}

			data += written;
			length -= static_cast<size_t>(written);
		}

		return true;
	}

	int openLogFile() {
		mkdir("HorizonOS:/Logs", 0755);
		return open("HorizonOS:/Logs/info.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
	}
}

int main() {
	if (register_horizonos_port(reinterpret_cast<long *>(&logdPort)) != 0 || logdPort == 0) {
		return 1;
	}

	if (!registerService("LogD")) {
		printf("LogD: failed to register service\n");
		return 1;
	}

	printf("LogD: ready\n");

	uint64_t lastSequence = 0;
	int logFd = -1;
	std::vector<std::string> backlog;
	backlog.reserve(4096);

	for (;;) {
		HorizonKernelLogEntry entries[64] {};
		uint64_t count = 0;

		if (readKernelLogs(lastSequence, entries, 64, &count) == 0 && count > 0) {
			for (uint64_t i = 0; i < count; ++i) {
				lastSequence = entries[i].sequence;
				backlog.push_back(formatEntry(entries[i]));
			}
		}

		if (logFd < 0) {
			logFd = openLogFile();
		}

		if (logFd >= 0 && !backlog.empty()) {
			size_t flushed = 0;

			for (; flushed < backlog.size(); ++flushed) {
				const std::string &line = backlog[flushed];

				if (!writeAll(logFd, line.data(), line.size())) {
					close(logFd);
					logFd = -1;
					break;
				}
			}

			if (flushed > 0) {
				backlog.erase(backlog.begin(), backlog.begin() + static_cast<long>(flushed));
			}
		}

		if (backlog.size() > 8192) {
			backlog.erase(backlog.begin(), backlog.begin() + static_cast<long>(backlog.size() - 8192));
		}

		usleep(25000);
	}
}
