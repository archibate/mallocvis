#pragma once

#include <cstddef>
#include <cstdint>

enum class AllocOp {
    New,
    Delete,
    NewArray,
    DeleteArray,
    Malloc,
    Free,
    CudaHostMalloc,
    CudaDeviceMalloc,
    CudaManagedMalloc,
    CudaFree,
    Unknown,
};

struct AllocAction {
    AllocOp op;
    uint32_t tid;
    void *ptr;
    size_t size;
    size_t align;
    void *caller;
    int64_t time;
};

constexpr const char *kAllocOpNames[] = {
    "New",
    "Delete",
    "NewArray",
    "DeleteArray",
    "Malloc",
    "Free",
    "CudaHostMalloc",
    "CudaDeviceMalloc",
    "CudaManagedMalloc",
    "CudaFree",
    "Unknown",
};

constexpr bool kAllocOpIsAllocation[] = {
    true,
    false,
    true,
    false,
    true,
    false,
    true,
    true,
    true,
    false,
    false,
};

constexpr bool kAllocOpIsCpp[] = {
    true,
    true,
    true,
    true,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
};

constexpr bool kAllocOpIsC[] = {
    false,
    false,
    false,
    false,
    true,
    true,
    false,
    false,
    false,
    false,
    false,
};

constexpr bool kAllocOpIsCuda[] = {
    false,
    false,
    false,
    false,
    false,
    false,
    true,
    true,
    true,
    true,
    false,
};

constexpr AllocOp kAllocOpFreeFunction[] = {
    AllocOp::Delete,
    AllocOp::Unknown,
    AllocOp::DeleteArray,
    AllocOp::Unknown,
    AllocOp::Malloc,
    AllocOp::Unknown,
    AllocOp::CudaFree,
    AllocOp::CudaFree,
    AllocOp::CudaFree,
    AllocOp::Unknown,
    AllocOp::Unknown,
};

constexpr size_t kNone = (size_t)-1;
