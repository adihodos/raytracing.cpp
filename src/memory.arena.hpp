#pragma once

#include <cstddef>
#include <cassert>
#include <cstdint>

[[nodiscard]] constexpr uintptr_t inline ptr_align(uintptr_t addr, size_t alignment) noexcept
{
    assert(alignment != 0);
    // return (addr + (alignment - 1)) & (-alignment);
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

struct MemoryArena
{
    std::byte* blk_addr;
    size_t blk_size;
    ptrdiff_t blk_offset{ 0 };

    MemoryArena(std::byte* addr, size_t size) noexcept
        : blk_addr{ addr }
        , blk_size{ size }
    {
    }

    MemoryArena(std::span<std::byte> blk) noexcept
        : MemoryArena{ blk.data(), blk.size() }
    {
    }

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    [[nodiscard]] void* mem_alloc(size_t bytes, size_t alignment) noexcept
    {
        if (bytes == 0)
            return nullptr;

        const uintptr_t unaligned_addr = reinterpret_cast<uintptr_t>(blk_addr) + blk_offset;
        const uintptr_t aligned_addr = ptr_align(unaligned_addr, alignment);
        const uintptr_t arena_end_addr = reinterpret_cast<uintptr_t>(blk_addr) + blk_size;

        if ((aligned_addr + bytes) > arena_end_addr) {
            //
            // arena full
            // TODO: handle this in the future
            return nullptr;
        }

        blk_offset = (aligned_addr + bytes) - reinterpret_cast<uintptr_t>(blk_addr);
        return reinterpret_cast<void*>(aligned_addr);
    }

    void mem_free(void* ptr, size_t bytes) noexcept
    {
        const uintptr_t arena_end_addr = reinterpret_cast<uintptr_t>(blk_addr) + blk_offset;
        const uintptr_t obj_end_addr = reinterpret_cast<uintptr_t>(ptr) + bytes;

        assert(obj_end_addr <= arena_end_addr);

        if (obj_end_addr == arena_end_addr) {
            blk_offset -= bytes;
        }
    }

    void reset() noexcept { blk_offset = 0; }
};

template<typename T>
class SimpleArenaAllocator
{
  public:
    using value_type = T;

    SimpleArenaAllocator(MemoryArena* mem_arena) noexcept
        : _mem_arena{ mem_arena }
    {
    }

    SimpleArenaAllocator(MemoryArena& mem_arena) noexcept
        : SimpleArenaAllocator{ &mem_arena }
    {
    }

    template<typename U>
    SimpleArenaAllocator(const SimpleArenaAllocator<U>& rhs) noexcept
        : SimpleArenaAllocator{ rhs._mem_arena }
    {
    }

    [[nodiscard]] T* allocate(size_t n) noexcept
    {
        return static_cast<T*>(_mem_arena->mem_alloc(n * sizeof(T), alignof(T)));
    }
    void deallocate(T* ptr, size_t n) noexcept { _mem_arena->mem_free(ptr, n * sizeof(T)); }

    inline bool operator==(const SimpleArenaAllocator<T>& rhs) noexcept { return this->_mem_arena == rhs._mem_arena; }
    inline bool operator!=(const SimpleArenaAllocator<T>& rhs) noexcept { return !(*this == rhs); }

  private:
    MemoryArena* _mem_arena;
};
