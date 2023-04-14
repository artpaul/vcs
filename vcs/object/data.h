#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace Vcs {

/**
 * Types of data objects.
 */
enum class DataType : uint8_t {
    None = 0,
    /// Content object.
    Blob = 1,
    /// Tree object.
    Tree = 2,
    /// Commit object.
    Commit = 3,
    /// History adjustment object.
    Renames = 4,
    /// Tag object.
    Tag = 5,
    /// Index object.
    Index = 15,
};

/**
 * @note The data model supports objects up to 256 Terabytes in size.
 */
class DataHeader {
public:
    union {
        struct {
            uint8_t tag;
            uint8_t size[7];
        };
        uint8_t data[8];
    };

    /** Makes DataHeader instance. */
    static constexpr DataHeader Make(const DataType type, const uint64_t size) {
        const uint8_t bytes = CountBytes(size);
        DataHeader result{};
        // Type tag.
        result.tag = (bytes << 4) | uint8_t(type);
        // Pack size.
        switch (bytes) {
            case 8:
            case 7:
                throw std::invalid_argument("the value of the size exceeds 48 bit");
            case 6:
                result.size[5] = (size >> 40) & 0xFF;
            case 5:
                result.size[4] = (size >> 32) & 0xFF;
            case 4:
                result.size[3] = (size >> 24) & 0xFF;
            case 3:
                result.size[2] = (size >> 16) & 0xFF;
            case 2:
                result.size[1] = (size >> 8) & 0xFF;
            case 1:
                result.size[0] = (size & 0xFF);
        }
        return result;
    }

public:
    /** Returns count of packed bytes. */
    constexpr size_t Bytes() const noexcept {
        return 1 + ((tag >> 4) & 0x07);
    }

    constexpr auto Data() const noexcept -> const uint8_t (&)[8] {
        return data;
    }

    /** Unpacks type of the object. */
    constexpr DataType Type() const noexcept {
        return DataType(tag & 0x0F);
    }

    /** Unpacks size of the object. */
    constexpr uint64_t Size() const noexcept {
        uint64_t result = 0;
        switch ((tag >> 4) & 0x07) {
            case 7:
                result |= uint64_t(size[6]) << 48;
            case 6:
                result |= uint64_t(size[5]) << 40;
            case 5:
                result |= uint64_t(size[4]) << 32;
            case 4:
                result |= uint64_t(size[3]) << 24;
            case 3:
                result |= uint64_t(size[2]) << 16;
            case 2:
                result |= uint64_t(size[1]) << 8;
            case 1:
                result |= uint64_t(size[0]);
        }
        return result;
    }

private:
    static constexpr uint8_t CountBytes(const uint64_t size) noexcept {
        if (size == 0) {
            return 0;
        }
        if (size & 0xFFFFFFFF00000000) {
            if (size & 0xFF00000000000000) {
                return 8;
            }
            if (size & 0x00FF000000000000) {
                return 7;
            }
            if (size & 0x0000FF0000000000) {
                return 6;
            }
            return 5;
        } else {
            if (size & 0x00000000FF000000) {
                return 4;
            }
            if (size & 0x0000000000FF0000) {
                return 3;
            }
            if (size & 0x000000000000FF00) {
                return 2;
            }
            return 1;
        }
    }
};

/// Ensure the value of DataHeader is memcpy copyable.
static_assert(std::is_trivial<DataHeader>::value);

/// Ensure DataHeader::Data() returns pointer to an array of fixed size.
static_assert(std::is_bounded_array_v<std::remove_reference_t<decltype(DataHeader().Data())>>);

} // namespace Vcs
