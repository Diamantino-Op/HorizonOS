extern "C" {
	void *__dso_handle = nullptr;

	int __cxa_atexit(void (*)(void *), void *, void *) {
		return 0;
	}

	void __cxa_finalize(void *) {
	}
}

