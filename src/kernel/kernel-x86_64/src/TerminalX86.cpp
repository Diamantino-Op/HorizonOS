#include "Terminal.hpp"

#include "hal/Cpu.hpp"
#include "hal/Interrupts.hpp"

namespace kernel::common {
	using namespace x86_64::hal;

	ExecutionNode *Terminal::getCurrentCore() {
		return &CpuManager::getCurrentCore()->executionNode;
	}

	void Terminal::printInterruptFrame(u64 *framePtr) {
		auto *frame = reinterpret_cast<Frame *>(framePtr);

		this->printfE9(true, "\o{33}[0;34m-   Interrupt Frame:");
		this->printfE9(true, "\o{33}[0;34m-   r15: 0x%.16lx", frame->r15);
		this->printfE9(true, "\o{33}[0;34m-   r14: 0x%.16lx", frame->r14);
		this->printfE9(true, "\o{33}[0;34m-   r13: 0x%.16lx", frame->r13);
		this->printfE9(true, "\o{33}[0;34m-   r12: 0x%.16lx", frame->r12);
		this->printfE9(true, "\o{33}[0;34m-   r11: 0x%.16lx", frame->r11);
		this->printfE9(true, "\o{33}[0;34m-   r10: 0x%.16lx", frame->r10);
		this->printfE9(true, "\o{33}[0;34m-   r9 : 0x%.16lx", frame->r9);
		this->printfE9(true, "\o{33}[0;34m-   r8 : 0x%.16lx", frame->r8);
		this->printfE9(true, "\o{33}[0;34m-   rbp: 0x%.16lx", frame->rbp);
		this->printfE9(true, "\o{33}[0;34m-   rdi: 0x%.16lx", frame->rdi);
		this->printfE9(true, "\o{33}[0;34m-   rsi: 0x%.16lx", frame->rsi);
		this->printfE9(true, "\o{33}[0;34m-   rdx: 0x%.16lx", frame->rdx);
		this->printfE9(true, "\o{33}[0;34m-   rcx: 0x%.16lx", frame->rcx);
		this->printfE9(true, "\o{33}[0;34m-   rbx: 0x%.16lx", frame->rbx);
		this->printfE9(true, "\o{33}[0;34m-   rax: 0x%.16lx", frame->rax);
		this->printfE9(true, "\o{33}[0;34m-   int: 0x%.16lx", frame->intNo);
		this->printfE9(true, "\o{33}[0;34m-   err: 0x%.16lx", frame->errNo);
		this->printfE9(true, "\o{33}[0;34m-   rip: 0x%.16lx", frame->rip);
		this->printfE9(true, "\o{33}[0;34m-   cs: 0x%.16lx", frame->cs);
		this->printfE9(true, "\o{33}[0;34m-   rFlags: 0x%.16lx", frame->rFlags);
		this->printfE9(true, "\o{33}[0;34m-   rsp: 0x%.16lx", frame->rsp);
		this->printfE9(true, "\o{33}[0;34m-   ss: 0x%.16lx", frame->ss);
	}
}