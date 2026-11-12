// SPDX-License-Identifier: BSD-3-Clause

extern "C" {
__constant__ unsigned long long chrono_batch_render_dispatch_args;
}

__device__ __forceinline__ bool chrono_batch_render_outside_dispatch() {
    if (chrono_batch_render_dispatch_args == 0) {
        return false;
    }
    const unsigned* counts = reinterpret_cast<const unsigned*>(chrono_batch_render_dispatch_args);
    return blockIdx.x >= counts[0] || blockIdx.y >= counts[1] || blockIdx.z >= counts[2];
}

__device__ __forceinline__ unsigned chrono_batch_render_linear_group_index() {
    unsigned x = gridDim.x;
    unsigned y = gridDim.y;
    if (chrono_batch_render_dispatch_args != 0) {
        const unsigned* counts = reinterpret_cast<const unsigned*>(chrono_batch_render_dispatch_args);
        x = counts[0];
        y = counts[1];
    }
    return (blockIdx.z * y + blockIdx.y) * x + blockIdx.x;
}
