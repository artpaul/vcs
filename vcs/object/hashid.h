#pragma once

#include "data.h"

#include <contrib/fmt/fmt/format.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace Vcs {

class HashId {
public:
    class Builder {
    public:
        Builder();
        ~Builder();

        Builder& Append(const DataHeader header) noexcept;
        Builder& Append(const void* data, const size_t len) noexcept;
        Builder& Append(const std::string_view data) noexcept;

        HashId Build() noexcept;

    private:
        class Impl;

        std::unique_ptr<Impl> impl_;
    };

public:
    /** Copies hash data from the provided location. */
    static HashId FromBytes(const void* data, const size_t len);

    /** Copies hash data from the provided location. */
    static HashId FromBytes(const std::string_view data);

    /** Copies hash data from the provided location. */
    static HashId FromBytes(const unsigned char (&data)[20]) noexcept;

    /** Parse hex representation of the id. */
    static HashId FromHex(const std::string_view hex);

    /** Checks whether the string is a valid representation of an id in bytes format. */
    static bool IsBytes(const std::string_view hex) noexcept;

    /** Checks whether the string is a valid representation of an id in hex format. */
    static bool IsHex(const std::string_view hex) noexcept;

    /** Makes canonical object hash. */
    static HashId Make(const DataType type, const std::string_view content);

    /** Maximum value of HashId. */
    static HashId Max() noexcept;

    /** Minimum value of HashId. */
    static HashId Min() noexcept;

public:
    constexpr auto Data() const noexcept -> const unsigned char (&)[20] {
        return data_;
    }

    /** Byte size of the data. */
    constexpr size_t Size() const noexcept {
        return sizeof(data_);
    }

    /** Hex representation of the hash. */
    std::string ToHex() const;

    /** Raw data of the hash. */
    std::string ToBytes() const;

public:
    explicit operator bool() const noexcept {
        static constexpr unsigned char zeroes[20] = {};
        // Ensure same size.
        static_assert(sizeof(zeroes) == sizeof(data_));
        // Check for non null.
        return std::memcmp(zeroes, data_, sizeof(data_)) != 0;
    }

    bool operator<(const HashId& other) const noexcept {
        return std::memcmp(data_, other.data_, sizeof(data_)) < 0;
    }

    bool operator==(const HashId& other) const noexcept {
        if (this == &other) {
            return true;
        }
        return std::memcmp(data_, other.data_, sizeof(data_)) == 0;
    }

    friend std::ostream& operator<<(std::ostream& output, const HashId& id);

private:
    alignas(alignof(uint32_t)) unsigned char data_[20];
};

/// Ensure the value of HashId is 20 bytes long.
static_assert(sizeof(HashId) == 20);

/// Ensure the value of HashId is memcpy copyable.
static_assert(std::is_trivially_copyable<HashId>::value);

/// Ensure HashId is 32-bit aligned.
static_assert(std::alignment_of<HashId>::value == std::alignment_of<uint32_t>::value);

/// Ensure HashId::Data() returns pointer to an array of fixed size.
static_assert(std::is_bounded_array_v<std::remove_reference_t<decltype(HashId().Data())>>);

} // namespace Vcs

template <>
struct fmt::formatter<Vcs::HashId> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const Vcs::HashId& id, FormatContext& ctx) {
        return fmt::formatter<std::string>::format(id.ToHex(), ctx);
    }
};

template <>
class std::hash<Vcs::HashId> {
public:
    std::size_t operator()(const Vcs::HashId& id) const noexcept {
        constexpr auto align_offset = (std::alignment_of_v<std::size_t> - std::alignment_of_v<Vcs::HashId>);
        // Ensure align_offset is positive.
        static_assert(std::alignment_of_v<std::size_t> >= std::alignment_of_v<Vcs::HashId>);
        // Ensure no buffer overrun.
        static_assert(sizeof(decltype(id.Data())) >= ((sizeof(std::size_t) + align_offset)));

        return *std::bit_cast<const std::size_t*>(
            std::bit_cast<const unsigned char*>(&id.Data()) + align_offset
        );
    }
};
