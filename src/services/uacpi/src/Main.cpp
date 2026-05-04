#include <sys/io.h>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
	if (ioperm(0x3F8, 8, 1)) { // enable access to serial port COM1
		return 1;
	}

	return 0;
}