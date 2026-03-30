#ifndef KERNEL_X86_64_PROFILERX86_HPP
#define KERNEL_X86_64_PROFILERX86_HPP

#include "SpinLock.hpp"
#include "Types.hpp"

namespace kernel::x86_64::utils {
	constexpr u64 maxProfRecords = 0x10000;
	constexpr u64 maxFrames = 1024;

	struct ProfRecord {
		void *fn;
		u64 total;
		usize calls;
	};

	struct CallFrame {
		void *fn; // function
		void *site; // call site
		u64 start; // tsc value when the function was called
		u64 ptime; // total time spent in profiler code across the entire call stack below this function, subtracted from runtime when adding to records
	};

	class Profiler {
	public:
		__attribute__((no_instrument_function)) static void start();
		__attribute__((no_instrument_function)) static void stop();
		__attribute__((no_instrument_function)) static void reset();
		__attribute__((no_instrument_function)) static void show(const char *name);

		__attribute__((no_instrument_function)) static u64 lock();
		__attribute__((no_instrument_function)) static void unlock(u64 hadInts);

	private:
		__attribute__((no_instrument_function)) static bool pred(const ProfRecord *a, const ProfRecord *b);

	public:
		static ProfRecord records[maxProfRecords];

		static u64 numRecords;

		static bool locked;

		static bool active;
	};

	__attribute__((no_instrument_function)) extern "C" void __cyg_profile_func_enter(void *fn, void *callSite);
	__attribute__((no_instrument_function)) extern "C" void __cyg_profile_func_exit(void *fn, void *callSite);
}

#endif
