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

public:
    constexpr auto Data() const noexcept -> const unsigned char (&)[20] {
        return data_;
    }

    constexpr size_t Size() const noexcept {
        return sizeof(data_);
    }

    /** Hex representation of the hash. */
    std::string ToHex() const;

    /** Raw data of the hash. */
    std::string ToBytes() const;

public:
    explicit operator bool() const noexcept {
        uint32_t data[5];
        // Ensure same size.
        static_assert(sizeof(data) == sizeof(data_));
        // Type punning.
        std::memcpy(data, data_, sizeof(data_));
        // Check for non null.
        return data[0] | data[1] | data[2] | data[3] | data[4];
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
        return formatter<std::string>::format(id.ToHex(), ctx);
    }
};

template <>
class std::hash<Vcs::HashId> {
public:
    std::size_t operator()(const Vcs::HashId& id) const noexcept {
        size_t hash;
        std::memcpy(&hash, id.Data() + 4, sizeof(hash));
        return hash;
    }
};
