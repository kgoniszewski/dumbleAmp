#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

#if defined (__clang__)
 #pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

namespace
{
    thread_local int guardDepth = 0;
    std::atomic<std::size_t> allocations { 0 }, deallocations { 0 };

    void* allocate (std::size_t size)
    {
        if (guardDepth > 0)
            allocations.fetch_add (1, std::memory_order_relaxed);

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }

    void* allocateAligned (std::size_t size, std::align_val_t alignment)
    {
        if (guardDepth > 0)
            allocations.fetch_add (1, std::memory_order_relaxed);

        const auto align = static_cast<std::size_t> (alignment);
        const auto rounded = (size + align - 1) / align * align;

        if (auto* p = std::aligned_alloc (align, rounded == 0 ? align : rounded))
            return p;

        throw std::bad_alloc();
    }

    void release (void* p) noexcept
    {
        if (p != nullptr && guardDepth > 0)
            deallocations.fetch_add (1, std::memory_order_relaxed);

        std::free (p);
    }
}

namespace dumble::test
{
AllocationGuard::AllocationGuard()  { ++guardDepth; }
AllocationGuard::~AllocationGuard() { --guardDepth; }

std::size_t AllocationGuard::getAllocationCount() noexcept   { return allocations.load(); }
std::size_t AllocationGuard::getDeallocationCount() noexcept { return deallocations.load(); }
void AllocationGuard::resetCounts() noexcept                 { allocations = 0; deallocations = 0; }
} // namespace dumble::test

void* operator new (std::size_t size)                                     { return allocate (size); }
void* operator new[] (std::size_t size)                                   { return allocate (size); }
void* operator new (std::size_t size, std::align_val_t a)                 { return allocateAligned (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a)               { return allocateAligned (size, a); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept     { try { return allocate (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept   { try { return allocate (size); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                                   { release (p); }
void operator delete[] (void* p) noexcept                                 { release (p); }
void operator delete (void* p, std::size_t) noexcept                      { release (p); }
void operator delete[] (void* p, std::size_t) noexcept                    { release (p); }
void operator delete (void* p, std::align_val_t) noexcept                 { release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept               { release (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept    { release (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept  { release (p); }
