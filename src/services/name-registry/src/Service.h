#ifndef HORIZONOS_SERVICE_H
#define HORIZONOS_SERVICE_H

#include <stdint.h>

typedef struct Service {
	uint64_t port;
	uint64_t ownerPid;
	uint64_t tid;
	char *name;
	uint64_t versionMajor;
	uint64_t versionMinor;
	uint64_t versionPatch;
} Service;

#endif

