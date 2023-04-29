#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>

/**
 * Memory arena.
 *
 * The class is intended for the allocation of many small objects.
 */
class Arena {
    struct Chunk {
        std::byte* ptr;
        /// Number of bytes left so far.
        size_t left;
        /// Pointer to a previous chunk in the list of allocated chunks.
        Chunk* const prev;

        constexpr Chunk(const size_t len, Chunk* previous) noexcept
            : ptr(std::bit_cast<std::byte*>(this + 1))
            , left(len)
            , prev(previous) {
        }

        static constexpr std::byte* AlignUp(std::byte* len, const size_t align) noexcept {
            return std::bit_cast<std::byte*>((std::bit_cast<uintptr_t>(len) + (align - 1)) & ~(align - 1));
        }

        /**
         * Allocates memory region at the current address.
         */
        constexpr std::byte* Allocate(size_t len) noexcept {
            if (left >= len) {
                std::byte* const ret = ptr;
                ptr += len;
                left -= len;
                return ret;
            }
            return nullptr;
        }

        /**
         * Allocates memory region at the aligned address.
         */
        constexpr std::byte* Allocate(const size_t len, const size_t align) noexcept {
            const size_t padding = AlignUp(ptr, align) - ptr;

            if (auto ret = Allocate(len + padding)) {
                return ret + padding;
            } else {
                return nullptr;
            }
        }
    };

    /// Ensure there is no need to explicitly call destructor for the object.
    static_assert(std::is_trivially_destructible_v<Chunk>);

public:
    constexpr Arena(const size_t page_size, const bool round_to_power_of_two = true) noexcept
        : empty_(0, nullptr)
        , current_(&empty_)
        , block_size_(std::max(page_size, sizeof(Chunk)) - sizeof(Chunk))
        , round_to_power_of_two_(round_to_power_of_two) {
    }

    ~Arena() {
        Clear();
    }

    void Clear() {
        while (current_->prev) {
            auto prev = current_->prev;
            // Release allocated memory for the chunk.
            delete[] std::bit_cast<std::byte*>(current_);
            // Move to previous chunk.
            current_ = prev;
        }

        assert(current_ == &empty_);
    }

public:
    void* Allocate(const size_t len) {
        return AllocateImpl(len);
    }

    void* Allocate(const size_t len, const size_t align) {
        return AllocateImpl(len, align);
    }

    /**
     * Allocates memory suitable to place object of type T.
     */
    template <typename T>
    T* Allocate() {
        return std::bit_cast<T*>(Allocate(sizeof(T), alignof(T)));
    }

private:
    void* AllocateImpl(const size_t len) {
        // Try to allocate memory from the current chunk.
        if (void* const ret = current_->Allocate(len)) [[likely]] {
            return ret;
        }
        // Create new chunk suitable to hold requested size of bytes.
        AddChunk(len);
        // Allocate memory from the new chunk.
        return current_->Allocate(len);
    }

    void* AllocateImpl(const size_t len, const size_t align) {
        assert(align);
        // Try to allocate memory from the current chunk.
        if (void* const ret = current_->Allocate(len, align)) [[likely]] {
            return ret;
        }
        // Create new chunk suitable to hold requested size of bytes.
        AddChunk(len + align - 1);
        // Allocate memory from the new chunk.
        return current_->Allocate(len, align);
    }

    void AddChunk(const size_t hint) {
        const size_t data_length = std::max(block_size_, hint);
        const size_t byte_length = round_to_power_of_two_ ? (std::bit_ceil(sizeof(Chunk) + data_length))
                                                          : (sizeof(Chunk) + data_length);

        current_ = new (new std::byte[byte_length]) Chunk(data_length, current_);
    }

private:
    Chunk empty_;
    Chunk* current_;
    size_t block_size_;
    bool round_to_power_of_two_;
};
