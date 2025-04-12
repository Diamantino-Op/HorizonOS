#include "CommonMain.hpp"

using namespace kernel::common;

Terminal CommonMain::terminal;
uPtr CommonMain::stackTop;

namespace kernel::common {
	uPtr CommonMain::getStackTop() {
		return stackTop;
	}

	Terminal* CommonMain::getTerminal() {
		return &terminal;
	}
}