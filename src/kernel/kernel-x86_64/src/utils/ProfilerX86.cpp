#include "ProfilerX86.hpp"

#include "CommonMain.hpp"
#include "Terminal.hpp"
#include "hal/Cpu.hpp"

extern limine_module_request moduleRequest;

namespace kernel::x86_64::utils {
	using namespace common;
	using namespace hal;

	ProfEntry Profiler::profTable[maxProfFunctions] {};
	ProfEntry *Profiler::profTableHash[profFuncHashSize] {};

	ProfEdge Profiler::edgeTable[maxProfEdges] {};
	ProfEdge *Profiler::edgeTableHash[profEdgeHashSize] {};

	CallFrame Profiler::callStack[maxCallStack] {};

	u64 Profiler::profEntryCount = 0;
	u64 Profiler::edgeEntryCount = 0;

	int Profiler::callSp = 0;

	bool Profiler::active = false;
	bool Profiler::locked = false;

	void Profiler::start() {
		__atomic_store_n(&Profiler::active, true, __ATOMIC_RELEASE);
	}

	void Profiler::stop() {
		__atomic_store_n(&Profiler::active, false, __ATOMIC_RELEASE);
	}

	void Profiler::reset() {
		const u64 hadInts = lock();

		profEntryCount = 0;
		edgeEntryCount = 0;

		for (auto &i : profTable) {
			i = ProfEntry();
		}

		for (auto &i : profTableHash) {
			i = nullptr;
		}

		for (auto &i : edgeTable) {
			i = ProfEdge();
		}

		for (auto &i : edgeTableHash) {
			i = nullptr;
		}

		unlock(hadInts);
	}

	void Profiler::show(const char *profName) {
		Terminal *term = CommonMain::getTerminal();

		term->printfCOM2(false, "version: 1\n");
		term->printfCOM2(false, "creator: %s-Profiler\n", profName);
		term->printfCOM2(false, "events: Cycles\n\n");

		for (usize i = 0; i < profEntryCount; i++) {
			const ProfEntry *e = &profTable[i];

			if (e->fn == nullptr) {
				continue;
			}

			u64 offset;
			const char *name = findSymbol(reinterpret_cast<u64>(e->fn), &offset);

			term->printfCOM2(false, "fn=%s\n", name != nullptr ? name : "unknown");
			term->printfCOM2(false, "1 %lu\n", e->totalCycles);

			for (size_t j = 0; j < edgeEntryCount; j++) {
				const ProfEdge *edge = &edgeTable[j];

				if (edge->parent == e->fn) {
					const char *cname = findSymbol(reinterpret_cast<u64>(edge->child), &offset);

					term->printfCOM2(false, "cfn=%s\n", cname != nullptr ? cname : "unknown");
					term->printfCOM2(false, "calls=%lu 1\n", edge->calls);
					term->printfCOM2(false, "1 %lu\n", edge->totalCycles);
				}
			}

			term->printfCOM2(false, "\n");
		}

		term->debug("Finished writing...", "Profiler");
	}

	u64 Profiler::lock() {
		u64 value;

		asm volatile("pushfq; popq %0; cli" : "=rm"(value));
		while (__atomic_exchange_n(&locked, true, __ATOMIC_ACQUIRE)) {
			asm("pause");
		}

		return value;
	}

	void Profiler::unlock(u64 hadInts) {
		__atomic_store_n(&locked, false, __ATOMIC_RELEASE);

		if (hadInts & 0x200) {
			asm("sti");
		}
	}

	u64 Profiler::readTsc() {
		u32 a = 0;
		u32 d = 0;

		asm volatile ("lfence; rdtsc" : "=a"(a), "=d"(d));

		return static_cast<u64>(a) | (static_cast<u64>(d) << 32);
	}

	ProfEntry *Profiler::profileFindOrAdd(void *fn) {
		usize idx = profileHashFn(fn);

		for (usize n = 0; n < profFuncHashSize; n++) {
			ProfEntry *e = profTableHash[idx];

			if (e == nullptr) {
				if (profEntryCount >= maxProfFunctions) {
					return nullptr;
				}

				e = &profTable[profEntryCount++];
				e->fn = fn;
				e->totalCycles = 0;
				e->calls = 0;

				profTableHash[idx] = e;

				return e;
			}

			if (e->fn == fn) {
				return e;
			}

			idx++;
			if (idx == profFuncHashSize) {
				idx = 0;
			}
		}

		return nullptr;
	}

	ProfEdge *Profiler::edgeFindOrAdd(void *parent, void *child) {
		usize idx = edgeHashFn(parent, child);

		for (usize n = 0; n < profEdgeHashSize; n++) {
			ProfEdge *e = edgeTableHash[idx];

			if (e == nullptr) {
				if (edgeEntryCount >= maxProfEdges) {
					return nullptr;
				}

				e = &edgeTable[edgeEntryCount++];
				e->parent = parent;
				e->child = child;
				e->calls = 0;
				e->totalCycles = 0;

				edgeTableHash[idx] = e;
				return e;
			}

			if (e->parent == parent && e->child == child) {
				return e;
			}

			idx++;
			if (idx == profEdgeHashSize) {
				idx = 0;
			}
		}

		return nullptr;
	}

	usize Profiler::profileHashFn(void *fn) {
		return (reinterpret_cast<uPtr>(fn) >> 4) % profFuncHashSize;
	}

	usize Profiler::edgeHashFn(void *parent, void *child) {
		auto p = reinterpret_cast<uPtr>(parent);
		auto c = reinterpret_cast<uPtr>(child);

		return ((p >> 4) ^ (c >> 4)) % profEdgeHashSize;
	}

	bool Profiler::isSymbol(char *str) {
		constexpr auto strComp = "Symbol";

		u64 i = 0;

		for (const char *p = str; *p != '\0'; p++) {
			if (*p != strComp[i++]) {
				return false;
			}
		}

		return true;
	}

	const char* Profiler::findSymbol(u64 address, u64* offset) {
	    if (moduleRequest.response != nullptr && moduleRequest.response->module_count > 0) {
	        for (u64 i = 0; i < moduleRequest.response->module_count; i++) {
	        	if (moduleRequest.response->modules[0]->string == nullptr and not isSymbol(moduleRequest.response->modules[i]->string)) {
	        		continue;
	        	}

		        const auto *symFileAddr = static_cast<char *>(moduleRequest.response->modules[i]->address);
		        const u64 symFileSize = moduleRequest.response->modules[i]->size;

		        u64 bestAddr = 0;
		        const char *bestName = nullptr;
		        usize bestLen = 0;

		        const char *cur = symFileAddr;
		        const char *end = symFileAddr + symFileSize;

		        while (cur < end) {
		            // Find end of line
		            const char *lineEnd = cur;

		            while (lineEnd < end && *lineEnd != '\n') {
	            		lineEnd++;
		            }

		            const char *p = cur;

		            u64 symAddr = 0;
		            bool hasAddr = false;

		            while (p < lineEnd && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
		                u8 digit;

		                if (*p >= '0' && *p <= '9') {
	                		digit = *p - '0';
		                } else if (*p >= 'a' && *p <= 'f') {
	                		digit = *p - 'a' + 10;
		                } else {
	                		digit = *p - 'A' + 10;
		                }

		                symAddr = (symAddr << 4) | digit;
		                hasAddr = true;

		                p++;
		            }

		            if (hasAddr && p < lineEnd && *p == ' ') {
		                p++;

		                const char symType = *p;

		                p++;

		                if (p < lineEnd && *p == ' ') {
		                    p++;

		                    const auto nameLen = static_cast<usize>(lineEnd - p);

		                    const bool isCode = (symType == 'T' || symType == 't' || symType == 'W' || symType == 'w');

		                    if (isCode && symAddr <= address && symAddr >= bestAddr && nameLen > 0) {
		                        bestAddr = symAddr;
		                        bestName = p;
		                        bestLen  = nameLen;
		                    }
		                }
		            }

		            cur = lineEnd;

		            if (cur < end && *cur == '\n') {
	            		cur++;
		            }
		        }

		        if (bestName != nullptr) {
		            if (offset != nullptr) {
	            		*offset = address - bestAddr;
		            }

		            static char nameBuf[512];

		            usize copyLen = bestLen < sizeof(nameBuf) - 1 ? bestLen : sizeof(nameBuf) - 1;

		            while (copyLen > 0 && (bestName[copyLen - 1] == '\r' || bestName[copyLen - 1] == ' ')) {
	            		copyLen--;
		            }

	        		for (usize i = 0; i < copyLen; i++) {
	        			nameBuf[i] = bestName[i];
	        		}

		            nameBuf[copyLen] = '\0';

		            return nameBuf;
		        }
	        }
	    }

	    if (offset != nullptr) {
	    	*offset = 0;
	    }

	    return nullptr;
	}

	void __cyg_profile_func_enter(void *thisFn, void *callSite) {
		if (!__atomic_load_n(&Profiler::active, __ATOMIC_ACQUIRE)) {
			return;
		}

		const u64 hadInts = Profiler::lock();

		const u64 t = Profiler::readTsc();

		if (Profiler::callSp < maxCallStack) {
			ProfEdge *parentEdge = nullptr;

			/* record edge from parent -> this_fn */
			if (Profiler::callSp > 0) {
				void *parent = Profiler::callStack[Profiler::callSp - 1].fn;
				parentEdge = Profiler::edgeFindOrAdd(parent, thisFn);

				if (parentEdge != nullptr) {
					parentEdge->calls++;
				}
			}

			Profiler::callStack[Profiler::callSp].fn = thisFn;
			Profiler::callStack[Profiler::callSp].enterTime = t;
			Profiler::callStack[Profiler::callSp].parentEdge = parentEdge;

			Profiler::callSp++;
		}

		Profiler::unlock(hadInts);
	}

	void __cyg_profile_func_exit(void *thisFn, void *callSite) {
		const u64 hadInts = Profiler::lock();

		const u64 t = Profiler::readTsc();

		if (Profiler::callSp > 0) {
			Profiler::callSp--;

			void *fn = Profiler::callStack[Profiler::callSp].fn;
			const u64 enter = Profiler::callStack[Profiler::callSp].enterTime;

			ProfEdge *parentEdge = Profiler::callStack[Profiler::callSp].parentEdge;
			ProfEntry *e = Profiler::profileFindOrAdd(fn);

			if (e != nullptr) {
				const u64 delta = (t - enter);

				e->totalCycles += delta;
				e->calls++;

				if (parentEdge != nullptr) {
					parentEdge->totalCycles += delta;
				}
			}
		}

		Profiler::unlock(hadInts);
	}
}