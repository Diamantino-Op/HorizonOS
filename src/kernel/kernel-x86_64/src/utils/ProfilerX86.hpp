#ifndef KERNEL_X86_64_PROFILERX86_HPP
#define KERNEL_X86_64_PROFILERX86_HPP

#include "SpinLock.hpp"
#include "Types.hpp"

namespace kernel::x86_64::utils {
	constexpr int maxCallStack = 256;

	constexpr u64 maxProfFunctions = 8192;
	constexpr u64 maxProfEdges = 32768;

	constexpr u64 profFuncHashSize = maxProfFunctions * 2;
	constexpr u64 profEdgeHashSize = maxProfEdges * 2;

	struct ProfEntry {
		void *fn;
		u64 totalCycles;
		usize calls;
	};

	struct ProfEdge {
		void *parent;
		void *child;
		u64 calls;
		u64 totalCycles;
	};

	struct CallFrame {
		void *fn;
		u64 enterTime;
		ProfEdge *parentEdge;
	};

	class Profiler {
	public:
		__attribute__((no_instrument_function)) static void start();
		__attribute__((no_instrument_function)) static void stop();
		__attribute__((no_instrument_function)) static void reset();
		__attribute__((no_instrument_function)) static void show(const char *profName);

		__attribute__((no_instrument_function)) static u64 lock();
		__attribute__((no_instrument_function)) static void unlock(u64 hadInts);

		__attribute__((no_instrument_function)) static u64 readTsc();

		__attribute__((no_instrument_function)) static ProfEntry *profileFindOrAdd(void *fn);
		__attribute__((no_instrument_function)) static ProfEdge *edgeFindOrAdd(void *parent, void *child);

	private:
		__attribute__((no_instrument_function)) static usize profileHashFn(void *fn);
		__attribute__((no_instrument_function)) static usize edgeHashFn(void *parent, void *child);
		__attribute__((no_instrument_function)) static bool isSymbol(char *str);

	public:
		__attribute__((no_instrument_function)) static const char* findSymbol(u64 address, u64* offset);

		static ProfEntry profTable[maxProfFunctions];
		static ProfEntry *profTableHash[profFuncHashSize];

		static ProfEdge edgeTable[maxProfEdges];
		static ProfEdge *edgeTableHash[profEdgeHashSize];

		static CallFrame callStack[maxCallStack];

		static u64 profEntryCount;
		static u64 edgeEntryCount;

		static int callSp;

		static bool locked;

		static bool active;
	};

	__attribute__((no_instrument_function)) extern "C" void __cyg_profile_func_enter(void *fn, void *callSite);
	__attribute__((no_instrument_function)) extern "C" void __cyg_profile_func_exit(void *fn, void *callSite);
}

#endif
