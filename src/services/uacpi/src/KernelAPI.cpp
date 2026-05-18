#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <horizonos/generic.h>
#include <horizonos/syscall.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <ctime>
#include <array>
#include "unistd.h"

#include "uacpi/log.h"
#include "uacpi/kernel_api.h"

using namespace std;

extern uint64_t uacpiPort;
extern uint64_t pciPort;

constexpr uint64_t PCI_READ_MSG_TYPE = 0x20;
constexpr uint64_t PCI_READ_REPLY_MSG_TYPE = 0x30;
constexpr uint64_t PCI_WRITE_MSG_TYPE = 0x40;

struct PciReadMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
};

struct PciReadReplyMsgData {
	uint32_t data {};
};

struct PciWriteMsgData {
	uint8_t bus {};
	uint8_t dev {};
	uint8_t func {};
	uint16_t offset {};
	uint8_t width {};
	uint32_t data {};
};

struct UacpiIoRange {
	uacpi_io_addr base;
	uacpi_size len;
};

static bool ioRangeCheck(const uacpi_handle handle, const uacpi_size offset, const uacpi_size accessSize) {
	const auto *range = static_cast<UacpiIoRange *>(handle);
	return range && accessSize <= range->len && offset <= (range->len - accessSize);
}

struct WorkItem {
	uacpi_work_handler handler;
	uacpi_handle ctx;
};

static pthread_mutex_t workMutex;
static pthread_cond_t workCond;
static int pendingWork = 0;

__attribute__((constructor)) static void initWorkQueue() {
	pthread_mutex_init(&workMutex, nullptr);
	pthread_cond_init(&workCond, nullptr);
}

static void *workThreadFunc(void *arg) {
	auto *item = static_cast<WorkItem *>(arg);

	item->handler(item->ctx);

	delete item;

	pthread_mutex_lock(&workMutex);

	pendingWork--;

	if (pendingWork == 0) {
		pthread_cond_broadcast(&workCond);
	}

	pthread_mutex_unlock(&workMutex);

	return nullptr;
}

struct PciHandle {
	uacpi_pci_address address;
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *outRsdpAddress) {
	if (!outRsdpAddress) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const int err = get_rsdp(outRsdpAddress);

	return err == 0 ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
}

void *uacpi_kernel_map(const uacpi_phys_addr addr, const uacpi_size len) {
	uint64_t ret;

	if (mmap_phys(addr, len, &ret) != 0) {
		return nullptr;
	}

	return reinterpret_cast<void *>(ret);
}

void uacpi_kernel_unmap(void *addr, const uacpi_size len) {
	munmap(addr, len);
}

void *uacpi_kernel_alloc(const uacpi_size size) {
	return malloc(size);
}

void *uacpi_kernel_alloc_zeroed(const uacpi_size size) {
	return calloc(1, size);
}

void uacpi_kernel_free(void *mem) {
	free(mem);
}

void uacpi_kernel_log(const uacpi_log_level level, const uacpi_char* str) {
	switch (level) {
		case UACPI_LOG_ERROR:
			printf("\o{33}[0;31muACPI: \o{33}[0;37m%s", str);
			break;

		case UACPI_LOG_WARN:
			printf("\o{33}[0;33muACPI: \o{33}[0;37m%s", str);
			break;

		case UACPI_LOG_INFO:
			printf("\o{33}[0;34muACPI: \o{33}[0;37m%s", str);
			break;

		case UACPI_LOG_TRACE:
		case UACPI_LOG_DEBUG:
			printf("\o{33}[0;32muACPI: \o{33}[0;37m%s", str);
			break;
	}
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot() {
	struct timespec ts {};

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}

	return static_cast<uacpi_u64>(ts.tv_sec) * 1000000000ULL + static_cast<uacpi_u64>(ts.tv_nsec);
}

void uacpi_kernel_stall(uacpi_u8 uSec) {
	const uacpi_u64 start  = uacpi_kernel_get_nanoseconds_since_boot();
	const uacpi_u64 delta  = static_cast<uacpi_u64>(uSec) * 1000ULL;

	while ((uacpi_kernel_get_nanoseconds_since_boot() - start) < delta) {
		asm volatile("pause");
	}
}

// PCI

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
	if (!out_handle) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	auto *handle     = new PciHandle;
	handle->address  = address;
	*out_handle      = static_cast<uacpi_handle>(handle);

	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
	delete static_cast<PciHandle *>(handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
	if (!device or !value) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	// Send

	auto sendMsg    = hos_msg();

	auto sendData   = PciReadMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_READ_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciReadMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.width  = 8;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	// Recv

	auto recvMsg    = hos_msg();

	auto recvData   = PciReadReplyMsgData();

	recvMsg.buffer  = &recvData;
	recvMsg.length  = sizeof(PciReadReplyMsgData);

	auto filterOptions           = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_READ_REPLY_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	const int result = receive_horizonos_message(uacpiPort, &recvMsg, &filterOptions);

	//delete recvMsg;
	delete[] filterOptions.whiteListTypes;

	if (result != 0) {
		//delete recvData;

		return UACPI_STATUS_INTERNAL_ERROR;
	}

	*value = static_cast<uacpi_u8>(recvData.data);

	//delete recvData;

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
	if (!device or !value) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	// Send

	auto sendMsg    = hos_msg();

	auto sendData   = PciReadMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_READ_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciReadMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.width  = 16;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	// Recv

	auto recvMsg    = hos_msg();

	auto recvData   = PciReadReplyMsgData();

	recvMsg.buffer  = &recvData;
	recvMsg.length  = sizeof(PciReadReplyMsgData);

	auto filterOptions           = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_READ_REPLY_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	const int result = receive_horizonos_message(uacpiPort, &recvMsg, &filterOptions);

	//delete recvMsg;
	delete[] filterOptions.whiteListTypes;

	if (result != 0) {
		//delete recvData;

		return UACPI_STATUS_INTERNAL_ERROR;
	}

	*value = static_cast<uacpi_u16>(recvData.data);

	//delete recvData;

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
	if (!device or !value) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	// Send

	auto sendMsg    = hos_msg();

	auto sendData   = PciReadMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_READ_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciReadMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.width  = 32;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	// Recv

	auto recvMsg    = hos_msg();

	auto recvData   = PciReadReplyMsgData();

	recvMsg.buffer  = &recvData;
	recvMsg.length  = sizeof(PciReadReplyMsgData);

	auto filterOptions           = filter_options();

	filterOptions.whiteListTypes = new uint64_t[1]{ PCI_READ_REPLY_MSG_TYPE };
	filterOptions.whiteListCount = 1;

	const int result = receive_horizonos_message(uacpiPort, &recvMsg, &filterOptions);

	//delete recvMsg;
	delete[] filterOptions.whiteListTypes;

	if (result != 0) {
		//delete recvData;

		return UACPI_STATUS_INTERNAL_ERROR;
	}

	*value = recvData.data;

	//delete recvData;

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	if (!device) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	auto sendMsg    = hos_msg();

	auto sendData   = PciWriteMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_WRITE_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciWriteMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.data   = value;
	sendData.width  = 8;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	if (!device) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	auto sendMsg    = hos_msg();

	auto sendData   = PciWriteMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_WRITE_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciWriteMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.data   = value;
	sendData.width  = 16;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	if (!device) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	const auto *h = static_cast<PciHandle *>(device);

	auto sendMsg    = hos_msg();

	auto sendData   = PciWriteMsgData();

	sendMsg.port    = pciPort;
	sendMsg.type    = PCI_WRITE_MSG_TYPE;
	sendMsg.buffer  = &sendData;
	sendMsg.length  = sizeof(PciWriteMsgData);

	sendData.bus    = h->address.bus;
	sendData.dev    = h->address.device;
	sendData.func   = h->address.function;
	sendData.offset = offset;
	sendData.data   = value;
	sendData.width  = 32;

	send_horizonos_message(uacpiPort, pciPort, &sendMsg);

	//delete sendMsg;
	//delete sendData;

	return UACPI_STATUS_OK;
}

// IO

uacpi_status uacpi_kernel_io_map(const uacpi_io_addr base, const uacpi_size len, uacpi_handle *outHandle) {
	if (!outHandle) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	if (ioperm(base, len, 1) != 0) {
		return UACPI_STATUS_DENIED;
	}

	auto *range = new UacpiIoRange;

	range->base = base;
	range->len = len;

	*outHandle = static_cast<uacpi_handle>(range);

	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	auto *range = static_cast<UacpiIoRange *>(handle);

	ioperm(range->base, range->len, 0);

	delete range;
}

uacpi_status uacpi_kernel_io_read8(const uacpi_handle handle, const uacpi_size offset, uacpi_u8 *outValue) {
	if (!outValue || !ioRangeCheck(handle, offset, 1)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	*outValue = inb(static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(const uacpi_handle handle, const uacpi_size offset, uacpi_u16 *outValue) {
	if (!outValue || !ioRangeCheck(handle, offset, 2)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	*outValue = inw(static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(const uacpi_handle handle, const uacpi_size offset, uacpi_u32 *outValue) {
	if (!outValue || !ioRangeCheck(handle, offset, 4)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	*outValue = inl(static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(const uacpi_handle handle, const uacpi_size offset, uacpi_u8 inValue) {
	if (!ioRangeCheck(handle, offset, 1)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	outb(inValue, static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(const uacpi_handle handle, const uacpi_size offset, uacpi_u16 inValue) {
	if (!ioRangeCheck(handle, offset, 2)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	outw(inValue, static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(const uacpi_handle handle, const uacpi_size offset, uacpi_u32 inValue) {
	if (!ioRangeCheck(handle, offset, 4)) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	outl(inValue, static_cast<UacpiIoRange *>(handle)->base + offset);

	return UACPI_STATUS_OK;
}

// Threads

void uacpi_kernel_sleep(uacpi_u64 mSec) {
	struct timespec ts {
		.tv_sec  = static_cast<time_t>(mSec / 1000),
		.tv_nsec = static_cast<long>((mSec % 1000) * 1000000LL)
	};

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

uacpi_thread_id uacpi_kernel_get_thread_id() {
	return reinterpret_cast<uacpi_thread_id>(gettid());
}

uacpi_handle uacpi_kernel_create_mutex() {
	auto *mutex = new pthread_mutex_t;

	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0) {
		delete mutex;
		return nullptr;
	}

	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
		pthread_mutexattr_destroy(&attr);
		delete mutex;
		return nullptr;
	}

	if (pthread_mutex_init(mutex, &attr) != 0) {
		pthread_mutexattr_destroy(&attr);
		delete mutex;

		return nullptr;
	}

	pthread_mutexattr_destroy(&attr);

	return mutex;
}

void uacpi_kernel_free_mutex(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	auto *mutex = static_cast<pthread_mutex_t *>(handle);

	pthread_mutex_destroy(mutex);
	delete mutex;
}

uacpi_status uacpi_kernel_acquire_mutex(const uacpi_handle handle, const uacpi_u16 timeout) {
	if (!handle) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	auto *mutex = static_cast<pthread_mutex_t *>(handle);

	if (timeout == 0xFFFF) {
		return pthread_mutex_lock(mutex) == 0 ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
	}

	if (timeout == 0) {
		return pthread_mutex_trylock(mutex) == 0 ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;
	}

	struct timespec ts{};

	clock_gettime(CLOCK_REALTIME, &ts);

	ts.tv_sec  += timeout / 1000;
	ts.tv_nsec += static_cast<long>((timeout % 1000) * 1000000LL);

	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}

	const int ret = pthread_mutex_timedlock(mutex, &ts);

	if (ret == 0) {
		return UACPI_STATUS_OK;
	}

	if (ret == ETIMEDOUT) {
		return UACPI_STATUS_TIMEOUT;
	}

	return UACPI_STATUS_INTERNAL_ERROR;
}

void uacpi_kernel_release_mutex(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	pthread_mutex_unlock(static_cast<pthread_mutex_t *>(handle));
}

uacpi_bool uacpi_kernel_wait_for_event(const uacpi_handle handle, const uacpi_u16 timeout) {
	if (!handle) {
		return UACPI_FALSE;
	}

	auto *sem = static_cast<sem_t *>(handle);

	if (timeout == 0xFFFF) {
		return sem_wait(sem) == 0;
	}

	if (timeout == 0) {
		return sem_trywait(sem) == 0;
	}

	struct timespec ts{};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec  += timeout / 1000;
	ts.tv_nsec += static_cast<long>((timeout % 1000) * 1000000LL);

	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}

	return sem_timedwait(sem, &ts) == 0;
}

uacpi_handle uacpi_kernel_create_event() {
	auto *sem = new sem_t;

	if (sem_init(sem, 0, 0) != 0) {
		delete sem;

		return nullptr;
	}

	return sem;
}

void uacpi_kernel_free_event(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	sem_destroy(static_cast<sem_t *>(handle));
	delete static_cast<sem_t *>(handle);
}

void uacpi_kernel_signal_event(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	sem_post(static_cast<sem_t *>(handle));
}

void uacpi_kernel_reset_event(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	auto *sem = static_cast<sem_t *>(handle);

	while (sem_trywait(sem) == 0) {}
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type workType, uacpi_work_handler workHandler, uacpi_handle ctx) {
	(void) workType; // HorizonOS has one thread pool for now

	if (!workHandler) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	auto *item = new WorkItem;

	item->handler = workHandler;
	item->ctx = ctx;

	__atomic_fetch_add(&pendingWork, 1, __ATOMIC_RELEASE);

	pthread_t thread;

	if (pthread_create(&thread, nullptr, workThreadFunc, item) != 0) {
		__atomic_fetch_sub(&pendingWork, 1, __ATOMIC_ACQUIRE);

		delete item;

		return UACPI_STATUS_INTERNAL_ERROR;
	}

	pthread_detach(thread);

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion() {
	pthread_mutex_lock(&workMutex);

	while (pendingWork > 0) {
		pthread_cond_wait(&workCond, &workMutex);
	}

	pthread_mutex_unlock(&workMutex);
	return UACPI_STATUS_OK;
}

// Interrupts

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *request) {
	if (!request) {
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	switch (request->type) {
		case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
			return UACPI_STATUS_OK;

		case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
			fprintf(stderr, "\o{33}[0;31muACPI: \o{33}[0;37mFirmware fatal: type=0x%X code=0x%X arg=0x%llX",
					request->fatal.type,
					request->fatal.code,
					static_cast<unsigned long long>(request->fatal.arg));

			abort();

			return UACPI_STATUS_OK;

		default:
			return UACPI_STATUS_UNIMPLEMENTED;
	}
}

uacpi_status uacpi_kernel_install_interrupt_handler(const uacpi_u32 irq, const uacpi_interrupt_handler intHandler, const uacpi_handle ctx, uacpi_handle *out_irq_handle) {
	const int err = install_irq_handler(irq, intHandler, ctx, out_irq_handle);

	return err == 0 ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(const uacpi_interrupt_handler intHandler, const uacpi_handle irqHandle) {
	const int err = uninstall_irq_handler(intHandler, irqHandle);

	return err == 0 ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
}

uacpi_handle uacpi_kernel_create_spinlock() {
	auto *lock = new pthread_spinlock_t;

	if (pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE) != 0) {
		delete lock;

		return nullptr;
	}

	return lock;
}

void uacpi_kernel_free_spinlock(const uacpi_handle handle) {
	if (!handle) {
		return;
	}

	pthread_spin_destroy(static_cast<pthread_spinlock_t *>(handle));
	delete static_cast<pthread_spinlock_t *>(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(const uacpi_handle handle) {
	if (!handle) {
		return 0;
	}

	pthread_spin_lock(static_cast<pthread_spinlock_t *>(handle));

	return 1;
}

void uacpi_kernel_unlock_spinlock(const uacpi_handle handle, const uacpi_cpu_flags prevIF) {
	(void)prevIF;

	if (!handle) {
		return;
	}

	pthread_spin_unlock(static_cast<pthread_spinlock_t *>(handle));
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts() {
	set_int_status(false);

	return 1;
}

void uacpi_kernel_restore_interrupts(const uacpi_interrupt_state state) {
	set_int_status(true);
}