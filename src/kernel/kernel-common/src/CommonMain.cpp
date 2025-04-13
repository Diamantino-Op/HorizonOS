#include "CommonMain.hpp"

using namespace kernel::common;

Terminal CommonMain::terminal;

namespace kernel::common {
	Terminal* CommonMain::getTerminal() {
		return &terminal;
	}
}