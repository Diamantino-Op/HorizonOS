#include "NVMe.hpp"

// Stores the controller MMIO base for later register access.
void NvmeDriver::attachRegisters(volatile std::uint8_t* base) noexcept {
	mmioBase = base;
}

// Resets the controller and waits for it to report that it is ready.
bool NvmeDriver::resetController() noexcept {
	return false;
}

// Configures the admin submission queue and admin completion queue.
bool NvmeDriver::initializeAdminQueues() noexcept {
	return false;
}

// Submits an admin command and retrieves the matching completion entry.
bool NvmeDriver::submitAdminCommand(const Command&, CompletionEntry&) noexcept {
	return false;
}

// Reads the controller's identification structure.
bool NvmeDriver::identifyController() noexcept {
	return false;
}

// Reads the identification data for a specific namespace.
bool NvmeDriver::identifyNamespace(std::uint32_t) noexcept {
	return false;
}

// Issues a namespace read request.
bool NvmeDriver::read(std::uint32_t, std::uint64_t, void*, std::size_t) noexcept {
	return false;
}

// Issues a namespace write request.
bool NvmeDriver::write(std::uint32_t, std::uint64_t, const void*, std::size_t) noexcept {
	return false;
}

// Flushes outstanding writes for the selected namespace.
bool NvmeDriver::flush(std::uint32_t) noexcept {
	return false;
}

// Stops the controller and clears local driver state.
void NvmeDriver::shutdown() noexcept {
	mmioBase = nullptr;
}
