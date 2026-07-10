#include <abi-bits/hos_msg.h>
#include <horizonos/generic.h>
#include <horizonos/syscall.h>

#include <algorithm>
#include <cerrno>
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
	constexpr char LOG_DIR[] = "HorizonOS:/Logs";
	constexpr char LOG_PATH[] = "HorizonOS:/Logs/info.log";

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
		constexpr uint64_t NS_PER_MS = 1000ULL * 1000ULL;
		constexpr uint64_t MS_PER_SECOND = 1000ULL;
		constexpr uint64_t SECONDS_PER_MINUTE = 60ULL;
		constexpr uint64_t MINUTES_PER_HOUR = 60ULL;

		const uint64_t totalMs = entry.timestampNs / NS_PER_MS;
		const uint64_t milliseconds = totalMs % MS_PER_SECOND;
		const uint64_t totalSeconds = totalMs / MS_PER_SECOND;
		const uint64_t seconds = totalSeconds % SECONDS_PER_MINUTE;
		const uint64_t totalMinutes = totalSeconds / SECONDS_PER_MINUTE;
		const uint64_t minutes = totalMinutes % MINUTES_PER_HOUR;
		const uint64_t hours = totalMinutes / MINUTES_PER_HOUR;

		char line[512] {};
		snprintf(line, sizeof(line), "[%02llu:%02llu:%02llu.%03llu] INFO %s: %s\n",
			static_cast<unsigned long long>(hours),
			static_cast<unsigned long long>(minutes),
			static_cast<unsigned long long>(seconds),
			static_cast<unsigned long long>(milliseconds),
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

	bool statDirectory(const char *path) {
		struct stat st {};

		if (stat(path, &st) != 0) {
			return false;
		}

		return S_ISDIR(st.st_mode);
	}

	int openLogFile() {
		if (!statDirectory(LOG_DIR)) {
			if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) {
				const int mkdirErr = errno;

				if (!statDirectory(LOG_DIR)) {
					struct stat root {};
					const int rootErr = stat("HorizonOS:/", &root) == 0 ? 0 : errno;

					printf("LogD: mkdir %s failed: errno=%d rootStatErr=%d", LOG_DIR, mkdirErr, rootErr);
					fflush(stdout);

					return -1;
				}
			}
		}

		struct stat existing {};

		if (stat(LOG_PATH, &existing) != 0 && (errno == EIO || errno == EINVAL)) {
			const int statErr = errno;

			if (unlink(LOG_PATH) == 0) {
				printf("LogD: removed broken %s after stat errno=%d", LOG_PATH, statErr);
				fflush(stdout);
			}
		}

		const int fd = open(LOG_PATH, O_CREAT | O_RDWR | O_TRUNC, 0644);

		if (fd < 0) {
			const int openErr = errno;
			struct stat dir {};
			struct stat file {};
			const int dirErr = stat(LOG_DIR, &dir) == 0 ? 0 : errno;
			const int fileErr = stat(LOG_PATH, &file) == 0 ? 0 : errno;

			printf("LogD: open %s failed: errno=%d dirStatErr=%d fileStatErr=%d", LOG_PATH, openErr, dirErr, fileErr);
			fflush(stdout);

			return -1;
		}

		if (lseek(fd, 0, SEEK_END) < 0) {
			printf("LogD: seek %s failed: errno=%d", LOG_PATH, errno);
			fflush(stdout);

			close(fd);
			return -1;
		}

		fsync(fd);

		printf("LogD: writing kernel info log to %s", LOG_PATH);
		fflush(stdout);

		return fd;
	}
}

int main() {
	if (register_horizonos_port(reinterpret_cast<long *>(&logdPort)) != 0 || logdPort == 0) {
		return 1;
	}

	if (!registerService("LogD")) {
		printf("LogD: failed to register service");
		fflush(stdout);

		return 1;
	}

	printf("LogD: ready");
	fflush(stdout);

	uint64_t lastSequence = 0;
	int logFd = -1;
	uint64_t openAttempts = 0;
	uint32_t openRetryDelay = 0;
	uint32_t writeDelay = 0;
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
			if (openRetryDelay == 0) {
				logFd = openLogFile();
				++openAttempts;
				openRetryDelay = 40;

				if (logFd < 0 && (openAttempts % 5) == 1) {
					printf("LogD: waiting for %s", LOG_PATH);
					fflush(stdout);
				}
			} else {
				--openRetryDelay;
			}
		}

		if (logFd >= 0 && !backlog.empty() && writeDelay == 0) {
			size_t flushed = 0;

			for (; flushed < backlog.size(); ++flushed) {
				const std::string &line = backlog[flushed];

				if (!writeAll(logFd, line.data(), line.size())) {
					printf("LogD: write %s failed: errno=%d", LOG_PATH, errno);
					fflush(stdout);

					close(logFd);

					logFd = -1;
					openRetryDelay = 0;

					break;
				}
			}

			if (flushed > 0) {
				if (logFd >= 0) {
					fsync(logFd);
					writeDelay = 40;
				}

				backlog.erase(backlog.begin(), backlog.begin() + static_cast<long>(flushed));
			}
		}

		if (writeDelay > 0) {
			--writeDelay;
		}

		if (backlog.size() > 8192) {
			backlog.erase(backlog.begin(), backlog.begin() + static_cast<long>(backlog.size() - 8192));
		}

		usleep(25000);
	}
}
