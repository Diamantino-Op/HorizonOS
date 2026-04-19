#ifndef HORIZONOS_SERVICE_HPP
#define HORIZONOS_SERVICE_HPP

#include <cstdint>
#include <string>

using namespace std;

struct Service {
	uint64_t port;
	uint64_t ownerPid;
	uint64_t tid;
	string name;
	uint64_t versionMajor;
	uint64_t versionMinor;
	uint64_t versionPatch;

	Service(const uint64_t port, const uint64_t ownerPid, const uint64_t tid, const string &name, const uint64_t versionMajor, const uint64_t versionMinor, const uint64_t versionPatch) :
		port(port),
		ownerPid(ownerPid),
		tid(tid),
		name(name),
		versionMajor(versionMajor),
		versionMinor(versionMinor),
		versionPatch(versionPatch) {}
};

#endif
