#include "Terminal.hpp"

#include "hal/Cpu.hpp"

namespace kernel::common {
	using namespace x86_64::hal;

	ExecutionNode *Terminal::getCurrentCore() {
		return &CpuManager::getCurrentCore()->executionNode;
	}
}