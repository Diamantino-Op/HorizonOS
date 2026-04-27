# Microkernel Services: First Services & Porting uACPI to Userland

This document captures a concrete, step-by-step plan to: (1) prioritize the first user‑space services to implement for a microkernel-style HorizonOS, and (2) port the existing `uacpi` kernel component into a user service (`acpid`).

Summary / Goals
- Provide a minimal, pragmatic path to move ACPI logic out of kernel space while keeping only a tiny hardware-mediation HAL in the kernel.
- Provide the essential userland services and kernel syscalls/IPC primitives you need to build a stable microkernel architecture.

Checklist (high-level)
- [ ] Design/implement a small IPC primitive + name/registry service
- [ ] Implement `init`/service manager + logging/console user service
- [ ] Implement device/resource manager (IRQ broker, MMIO mapping broker)
- [ ] Implement controlled physical memory mapping syscall (map_phys/unmap)
- [ ] Implement IRQ registration/delivery kernel API to user services
- [ ] Create `acpid` user service and move ACPI parsing/AML to userland incrementally
- [ ] Test thoroughly (table parity, IRQ delivery, power actions)

Why these services first
1. IPC + name/registry service: everything in a microkernel depends on messaging; get this right first.
2. `init` / service manager: starts and supervises services and grants capabilities/tokens.
3. Device/resource manager: user services must not access hardware directly; kernel mediates and forwards IRQs and grants mappings.
4. Memory mapping helpers: user services (like `acpid`) must read ACPI tables and map MMIO safely and efficiently.
5. Drivers and subsystems: once plumbing exists you can move drivers (or user-mode drivers) out of the kernel.
6. Policy/capability system: needed to control privileges and avoid userland abuse.

Minimal kernel features required (before moving more into userland)
- Stable syscall/IPC primitive (synchronous RPC or message passing)
- IRQ registration/delivery (kernel forwards IRQs to a target process/endpoint)
- Kernel-controlled physical mapping syscall (map physical -> user virtual)
- Basic scheduler and preemption (already present)
- Optional: small capability/token system to control which process may request what mappings/IRQs

Porting `uacpi` to a user service — high level split
- Kernel HAL (privileged):
  - discover ACPI table physical addresses at boot
  - provide map_phys/unmap syscalls
  - accept IRQ registrations and deliver IRQ messages to user services
  - expose a tiny set of privileged RPCs for power operations (PM control register writes, reboot) if needed
- `acpid` (unprivileged user service):
  - ACPI table parsing (RSDP/RSDT/XSDT/FADT)
  - AML interpreter and device discovery/management
  - response to ACPI events (SCI) via IRQ messages or IPC
  - publish devices through the name/registry service and provide interfaces for other services to query ACPI topology

Incremental migration checklist (concrete)
1. Add a user process prototype `acpid` that runs but only consumes kernel-provided table pointers (kernel maps them at boot) and prints parsed results. No IRQs yet.
2. Implement `syscall_map_phys(phys, size, prot)` and `syscall_unmap(user_vaddr, size)` in the kernel.
   - Kernel enforces ranges (initially only ACPI table ranges and other safe areas) and returns ERR on policy violation.
3. Update `acpid` to call `syscall_map_phys()` to map the ACPI tables into its address space and parse them; validate parsed results match kernel-run data.
4. Implement IRQ registration in the kernel: `syscall_register_irq(gsi)` returning an irq token/handle; kernel forwards IRQ events as IPC/messages to the registered process.
5. Update `acpid` to `syscall_register_irq(sci_gsi)` and implement an event loop to receive IRQ messages and dispatch to AML handlers.
6. Implement or expose privileged RPCs (via limited syscalls) for power actions that must be performed by kernel or a trusted device manager.
7. After correctness is verified, remove kernel ACPI logic and keep `acpid` as the canonical ACPI manager.

Suggested syscall/API surface (examples)
Note: adapt numeric syscall IDs to your `SyscallManager` tables and calling conventions.

Syscalls
- map_phys
  - signature: long syscall_map_phys(void **ret_user_ptr, u64 phys_addr, u64 size, u64 prot_flags)
  - returns: 0 on success, and *ret_user_ptr is set to user-mapped pointer.
  - prot_flags: read-only / read-write / exec (ACPI should map read-only)

- unmap
  - signature: long syscall_unmap(u64 user_vaddr, u64 size)

- register_irq
  - signature: long syscall_register_irq(int *ret_token, u32 gsi, u32 flags)
  - returns a small token/handle used for management (or -errno on error)

- wait_irq (blocking) OR deliver IRQ messages via the kernel IPC queue
  - signature: long syscall_wait_irq(irq_message_t *out)
  - Prefer delivering IRQ messages via the IPC primitive — faster and more flexible.

- mmio_read/write (optional; if you allow mapping, prefer mapping)
  - signature examples for single operations (less efficient):
    - u64 syscall_mmio_read(u64 phys, u32 size)
    - long syscall_mmio_write(u64 phys, u64 value, u32 size)

- get_acpi_root
  - signature: long syscall_get_acpi_rsdp(void **ret_ptr)
  - convenience: kernel maps RSDP/XSDT/RSDT in process and returns pointer(s).

IPC message formats
- IRQ message (delivered to service):
  - struct IRQMessage { u32 type; u32 gsi; u64 ts; u64 extra; }
- Map response: map syscalls either return a pointer directly, or return a mapping handle and then an explicit pointer via another call.

Security / capability suggestions
- Kernel returns opaque handles for IRQ registrations and mappings; only the holder of the handle can unmap/unregister.
- Limit mapping ranges by policy: initially only ACPI table physical ranges; expand with audited code reviews.
- Consider an audit/logging facility that records which process mapped what physical ranges.

Concrete userland `acpid` skeleton (pseudocode)

1) bootstrap
- call syscall_get_acpi_rsdp(&ptr) or call syscall_map_phys(rsdp_phys, len, PROT_READ)
- parse RSDP, find XSDT/RSDT
- map table pages with syscall_map_phys
- parse tables (FADT, etc.)

2) IRQ event loop
- token = syscall_register_irq(sci_gsi)
- while (1) {
    // wait for messages/irq
    msg = ipc_recv(); // or syscall_wait_irq(&irq)
    if (msg.type == MSG_IRQ) handle_acpi_sci(msg.gsi);
}

3) publish devices
- using name service or registry, publish discovered devices and ACPI methods

Porting tips and gotchas
- ACPI tables often span multiple pages — map by page-aligned ranges (kernel enforces page alignment)
- ACPI AML may execute operations that need privileged access (PCI config / power control). Refactor these into kernel RPCs or into a privileged device manager process that `acpid` can request actions from.
- Prefer mapping a physical range once for batch reads rather than repeated small syscalls.
- Make IRQ delivery asynchronous (kernel posts a message into `acpid`'s message queue) to avoid blocking kernel threads.

Testing strategy
- Stage 0: run `acpid` user process that only parses mapped tables and prints a canonical dump; compare with kernel ACPI dump.
- Stage 1: allow IRQ mapping and validate SCI events delivered (simulate events if needed) and verify handler invocation.
- Stage 2: enable power actions in a safe, simulated environment (qemu snapshots) and verify behavior.

Example mapping usage (pseudocode)
```c
void *ptr;
if (syscall_map_phys(&ptr, rsdp_phys, rsdp_len, PROT_READ) != 0) {
    // handle error
}
parse_rsdp(ptr);
// when done
syscall_unmap((u64)ptr, rsdp_len);
```

Example IRQ registration (pseudocode)
```c
int token;
if (syscall_register_irq(&token, sci_gsi, 0) != 0) {
    // handle error
}
while (1) {
    IRQMessage msg;
    ipc_recv(&msg);
    if (msg.type == MSG_IRQ && msg.gsi == sci_gsi) {
        acpi_handle_sci();
    }
}
```

Next practical steps I can do for you (pick one or more):
- Draft the exact syscall numbers and kernel prototypes and add them to your `Syscall` table (`Syscall.cpp` / `SyscallManager::init()`), with kernel-side stub implementations.
- Produce a minimal `acpid` C source file (that compiles in your build) which maps RSDP and prints it.
- Implement kernel side of `syscall_map_phys` and `syscall_register_irq` (I can make the code edits and compile) as a first step.

If you want me to actually add the new markdown into the repository (or change kernel code to add syscalls), I have already created this file: `D:/HorizonOS/PDFs/Porting_uacpi_and_first_services.md` with the plan. Would you like me to also implement any of the syscall stubs now?
