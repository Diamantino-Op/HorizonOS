#include "ProfilerX86.hpp"

#include "Asm.hpp"
#include "CommonMain.hpp"
#include "Terminal.hpp"
#include "hal/Cpu.hpp"

namespace kernel::x86_64::utils {
	using namespace common;
	using namespace hal;

	ProfRecord Profiler::records[maxProfRecords];
	u64 Profiler::numRecords;
	u8 Profiler::active = 0;
	TicketSpinLock Profiler::lock {};

	void Profiler::start() {
		__atomic_store_n(&Profiler::active, 1, __ATOMIC_RELEASE);
	}

	void Profiler::stop() {
		__atomic_store_n(&Profiler::active, 0, __ATOMIC_RELEASE);
	}

	void Profiler::reset() {
		const bool hadInts = lock.lock();

		numRecords = 0;

		lock.unlock(hadInts);
	}

	void Profiler::show(const char *name) {
		const bool hadInts = lock.lock();

		Terminal *terminal = CommonMain::getTerminal();

		for (usize i = 1; i < numRecords; i++) {
			for (usize j = i; j > 0 && pred(&records[j - 1], &records[j]); j--) {
				ProfRecord *a = &records[j - 1];
				ProfRecord *b = &records[j];

				const ProfRecord temp = *a;
				*a = *b;
				*b = temp;
			}
		}

		terminal->info("Profiler results for '%s' (%lu records):", "Profiler", name, numRecords);

		for (size_t i = 0; i < numRecords; i++) {
			terminal->info("%lu) 0x%.16lx: %lu (%lu calls, avg %lu per call)", "Profiler",
					i + 1, reinterpret_cast<uPtr>(records[i].fn), records[i].total, records[i].calls,
					(records[i].total + (records[i].calls / 2)) / records[i].calls);
		}

		lock.unlock(hadInts);
	}

	bool Profiler::pred(const ProfRecord *a, const ProfRecord *b) {
		const usize ta = (a->total + (a->calls / 2)) / a->calls;
		const usize tb = (b->total + (b->calls / 2)) / b->calls;

		return ta < tb;
	}

	void __cyg_profile_func_enter(void *fn, void *callSite) {
		CpuCore *currentCore = CpuManager::getCurrentCore();

		const u64 start = currentCore->tsc.read();

		if (!__atomic_load_n(&Profiler::active, __ATOMIC_ACQUIRE)) {
			return;
		}

		const usize idx = currentCore->currFrame++;

		if (idx >= maxFrames) {
			Asm::cli();

			CommonMain::getTerminal()->error("Profiler frame limit of %lu exceeded!", "Profiler", maxFrames);

			Asm::lhlt();
		}

		CallFrame *frame = &currentCore->frames[idx];
		frame->fn = fn;
		frame->site = callSite;
		frame->ptime = 0;

		const u64 end = CpuManager::getCurrentCore()->tsc.read();
		const u64 time = end - start;

		for (usize i = 0; i < idx; i++) {
			currentCore->frames[i].ptime += time;
		}

		frame->start = CpuManager::getCurrentCore()->tsc.read();
	}

	void __cyg_profile_func_exit(void *fn, void *callSite) {
		CpuCore *currentCore = CpuManager::getCurrentCore();

		const u64 start = currentCore->tsc.read();

		if (!__atomic_load_n(&Profiler::active, __ATOMIC_ACQUIRE)) {
			return;
		}

		const bool hadInts = Profiler::lock.lock();

		const size_t idx = --currentCore->currFrame;
		const CallFrame *frame = &currentCore->frames[idx];
		uint64_t time = start - frame->start - frame->ptime;

		if (frame->fn != fn && frame->site != callSite) {
			Asm::cli();

			CommonMain::getTerminal()->error("Profiler function exit does not match any function entry! (fn: 0x%.16lx, callSite: 0x%.16lx)", "Profiler",
					reinterpret_cast<uPtr>(fn), reinterpret_cast<uPtr>(callSite));

			Asm::lhlt();
		}

		for (size_t i = 0; i < Profiler::numRecords; i++) {
			ProfRecord *record = &Profiler::records[i];

			if (record->fn == fn) {
				record->total += time;
				record->calls += 1;

				Profiler::lock.unlock(hadInts);

				return;
			}
		}

		if (Profiler::numRecords == maxProfRecords) {
			Asm::cli();

			CommonMain::getTerminal()->error("Profiler record limit of %lu exceeded!", "Profiler", maxProfRecords);

			Asm::lhlt();
		}

		ProfRecord *record = &Profiler::records[Profiler::numRecords++];
		record->fn = fn;
		record->total = time;
		record->calls = 1;

		Profiler::lock.unlock(hadInts);

		const uint64_t end = currentCore->tsc.read();
		time = end - start;

		for (size_t i = 0; i < idx; i++) {
			currentCore->frames[i].ptime += time;
		}
	}
}