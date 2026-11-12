// SPDX-License-Identifier: BSD-3-Clause

__device__ __forceinline__ unsigned long long __ballot_sync(unsigned mask, int predicate)
{
    return __ballot(predicate);
}
__device__ __forceinline__ int __any_sync(unsigned mask, int predicate)
{
    return __any(predicate);
}
__device__ __forceinline__ int __all_sync(unsigned mask, int predicate)
{
    return __all(predicate);
}
template<typename T>
__device__ __forceinline__ T __shfl_sync(unsigned mask, T value, int lane, int width = warpSize)
{
    return __shfl(value, lane, width);
}
template<typename T>
__device__ __forceinline__ T __shfl_xor_sync(unsigned mask, T value, int lane, int width = warpSize)
{
    return __shfl_xor(value, lane, width);
}
template<typename T>
__device__ __forceinline__ T
__shfl_up_sync(unsigned mask, T value, unsigned delta, int width = warpSize)
{
    return __shfl_up(value, delta, width);
}
template<typename T>
__device__ __forceinline__ T
__shfl_down_sync(unsigned mask, T value, unsigned delta, int width = warpSize)
{
    return __shfl_down(value, delta, width);
}
__device__ __forceinline__ void __syncwarp(unsigned mask = 0xffffffffu) {}

template<typename T>
struct ChronoBatchRenderIndex
{
    static constexpr bool value = false;
};
template<>
struct ChronoBatchRenderIndex<__hip_builtin_threadIdx_t>
{
    static constexpr bool value = true;
};
template<>
struct ChronoBatchRenderIndex<__hip_builtin_blockIdx_t>
{
    static constexpr bool value = true;
};
template<>
struct ChronoBatchRenderIndex<__hip_builtin_blockDim_t>
{
    static constexpr bool value = true;
};
template<>
struct ChronoBatchRenderIndex<__hip_builtin_gridDim_t>
{
    static constexpr bool value = true;
};
template<bool Condition>
struct ChronoBatchRenderEnable
{
};
template<>
struct ChronoBatchRenderEnable<true>
{
    typedef uint3 type;
};
template<typename T>
__device__ __forceinline__ uint3 ChronoBatchRenderUint3(const T& v)
{
    return make_uint3(v.x, v.y, v.z);
}

#define CHRONO_BATCH_RENDER_INDEX_OPERATOR(op, Index)                                                            \
    __device__ __forceinline__ uint3 operator op(const uint3& a, const Index& b)                   \
    {                                                                                              \
        return make_uint3(a.x op b.x, a.y op b.y, a.z op b.z);                                     \
    }                                                                                              \
    __device__ __forceinline__ uint3 operator op(const Index& a, const uint3& b)                   \
    {                                                                                              \
        return make_uint3(a.x op b.x, a.y op b.y, a.z op b.z);                                     \
    }
#define CHRONO_BATCH_RENDER_INDEX_OPERATORS(op)                                                                  \
    template<typename A, typename B>                                                               \
    __device__ __forceinline__                                                                     \
        typename ChronoBatchRenderEnable<ChronoBatchRenderIndex<A>::value && ChronoBatchRenderIndex<B>::value>::type                   \
        operator op(const A& a, const B& b)                                                        \
    {                                                                                              \
        const uint3 x = ChronoBatchRenderUint3(a);                                                             \
        const uint3 y = ChronoBatchRenderUint3(b);                                                             \
        return make_uint3(x.x op y.x, x.y op y.y, x.z op y.z);                                     \
    }                                                                                              \
    CHRONO_BATCH_RENDER_INDEX_OPERATOR(op, __hip_builtin_threadIdx_t)                                            \
    CHRONO_BATCH_RENDER_INDEX_OPERATOR(op, __hip_builtin_blockIdx_t)                                             \
    CHRONO_BATCH_RENDER_INDEX_OPERATOR(op, __hip_builtin_blockDim_t)                                             \
    CHRONO_BATCH_RENDER_INDEX_OPERATOR(op, __hip_builtin_gridDim_t)
CHRONO_BATCH_RENDER_INDEX_OPERATORS(+)
CHRONO_BATCH_RENDER_INDEX_OPERATORS(-)
CHRONO_BATCH_RENDER_INDEX_OPERATORS(*)
CHRONO_BATCH_RENDER_INDEX_OPERATORS(/)
CHRONO_BATCH_RENDER_INDEX_OPERATORS(%)
#undef CHRONO_BATCH_RENDER_INDEX_OPERATORS
#undef CHRONO_BATCH_RENDER_INDEX_OPERATOR

#define CHRONO_BATCH_RENDER_ATOMIC_REDUCE(op, function, T, U)                                                    \
    __device__ __forceinline__ void __slang_atomic_reduce_##op(T* addr, T val, int order)       \
    {                                                                                              \
        function(reinterpret_cast<U*>(addr), static_cast<U>(val));                                 \
    }
#define CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(op, function)                                                 \
    CHRONO_BATCH_RENDER_ATOMIC_REDUCE(op, function, int, int)                                                    \
    CHRONO_BATCH_RENDER_ATOMIC_REDUCE(op, function, unsigned, unsigned)                                          \
    CHRONO_BATCH_RENDER_ATOMIC_REDUCE(op, function, unsigned long long, unsigned long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(add, atomicAdd)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(and, atomicAnd)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(or, atomicOr)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(xor, atomicXor)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(min, atomicMin)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS(max, atomicMax)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(add, atomicAdd, long long, unsigned long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(and, atomicAnd, long long, unsigned long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(or, atomicOr, long long, unsigned long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(xor, atomicXor, long long, unsigned long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(min, atomicMin, long long, long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(max, atomicMax, long long, long long)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(add, atomicAdd, float, float)
CHRONO_BATCH_RENDER_ATOMIC_REDUCE(add, atomicAdd, double, double)
__device__ __forceinline__ void __slang_atomic_reduce_inc(unsigned* addr, int order)
{
    atomicAdd(addr, 1u);
}
__device__ __forceinline__ void __slang_atomic_reduce_inc(int* addr, int order)
{
    atomicAdd(addr, 1);
}
__device__ __forceinline__ void __slang_atomic_reduce_dec(unsigned* addr, int order)
{
    atomicSub(addr, 1u);
}
__device__ __forceinline__ void __slang_atomic_reduce_dec(int* addr, int order)
{
    atomicSub(addr, 1);
}
#undef CHRONO_BATCH_RENDER_ATOMIC_REDUCE_INTEGERS
#undef CHRONO_BATCH_RENDER_ATOMIC_REDUCE
