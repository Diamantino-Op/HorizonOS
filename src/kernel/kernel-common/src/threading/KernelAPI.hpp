#ifndef KERNEL_COMMON_KERNELAPI_HPP
#define KERNEL_COMMON_KERNELAPI_HPP

namespace kernel::common::api {
    class KernelAPI;

    // TODO: Add a way to get the thread and to alloc / dealloc (via message)
    struct KernelModule {
        KernelAPI *kernelAPI {};
    };

    struct MessageRequest {

    };

    struct MessageResponse {

    };

    class KernelAPI {

    };
}

#endif