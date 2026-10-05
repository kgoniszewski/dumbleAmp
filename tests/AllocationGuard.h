#pragma once

#include <atomic>
#include <cstddef>

namespace dumble::test
{
/**
    While an AllocationGuard is alive on the current thread, every global operator new/delete
    on that thread is counted. Used to prove processBlock-style code never touches the heap.
*/
struct AllocationGuard
{
    AllocationGuard();
    ~AllocationGuard();

    static std::size_t getAllocationCount() noexcept;
    static std::size_t getDeallocationCount() noexcept;
    static void resetCounts() noexcept;
};
} // namespace dumble::test
