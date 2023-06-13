#pragma once

#include <vcs/object/data.h>
#include <vcs/object/hashid.h>

#include <util/varint.h>

#include <limits>

namespace Vcs::Store::Disk {

/// Size of written objects should not exceed 134'217'728 bytes.
static constexpr size_t kMaximumContentSize = 128u << 20;

struct alignas(4) FileHeader {
    // |----------------------------------------------------------------|
    // |                  The layout of tag field                       |
    // |----------------------------------------------------------------|
    // | : 21 | compression : 3 | checksum : 1 | type : 4 | version : 3 |
    // |------|-----------------|--------------|----------|-------------|

    /// Packed metainformation about the file and the stored object.
    uint32_t tag;
    /// Original size of the object.
    uint32_t original;
    /// Size of stored data.
    /// Should be equal to original size in case of uncompressed data.
    uint32_t stored;
    /// Check sum of all previous fields.
    uint32_t crc;

public:
    static constexpr uint32_t MakeTag(const Compression compression, const DataType type) noexcept {
        return
            // Compression codec.
            ((uint32_t(compression) & 0x07) << 8)
            // Has trailing checksum.
            | (1u << 7)
            // Type of an object.
            | ((uint32_t(type) & 0x0F) << 3)
            // Version of file format.
            | (1u);
    }

    /** Type of compression method. */
    constexpr Compression Codec() const noexcept {
        return Compression((tag >> 8) & 0x07);
    }

    /** Size of stored object. */
    constexpr uint32_t Size() const noexcept {
        return original;
    }

    /** Type of stored object. */
    constexpr DataType Type() const noexcept {
        return DataType((tag >> 3) & 0x0F);
    }

    /** Version of a file format. */
    constexpr uint8_t Version() const noexcept {
        return tag & 0x07;
    }
};

/// Ensure FileHeader has expected size.
static_assert(sizeof(FileHeader) == 16);
/// Ensure the crc field is placed last.
static_assert(offsetof(FileHeader, crc) + sizeof(FileHeader::crc) == sizeof(FileHeader));

static_assert(std::numeric_limits<decltype(FileHeader::original)>::max() >= kMaximumContentSize);

struct alignas(alignof(HashId)) IndexEntry {
    /// Key.
    HashId id;
    /// Packed header and offset.
    std::byte tag[12];

    DataHeader GetMeta() const noexcept {
        DataHeader hdr;
        std::memcpy(&hdr, tag, sizeof(hdr));
        return DataHeader::Make(hdr.Type(), hdr.Size());
    }

    uint64_t GetOffset() const noexcept {
        DataHeader hdr;
        uint64_t value;
        std::memcpy(&hdr, tag, sizeof(hdr));
        DecodeVarint(reinterpret_cast<const uint8_t*>(tag) + hdr.Bytes(), sizeof(tag) - hdr.Bytes(), value);
        return value;
    }

    static IndexEntry Make(const HashId& id, const DataHeader hdr, const uint64_t offset) {
        IndexEntry entry{};
        // Set key.
        entry.id = id;
        // Set header.
        std::memcpy(entry.tag, hdr.Data(), hdr.Bytes());
        // Set offset.
        if (!EncodeVarint(
                offset, reinterpret_cast<uint8_t*>(entry.tag) + hdr.Bytes(), sizeof(entry.tag) - hdr.Bytes()
            ))
        {
            throw std::runtime_error("cannot pack offset");
        }

        return entry;
    }
};

/// Ensure IndexEntry has expected size.
static_assert(sizeof(IndexEntry) == 32);
/// Ensure HashId is 32-bit aligned.
static_assert(std::alignment_of<IndexEntry>::value == std::alignment_of<uint32_t>::value);

} // namespace Vcs::Store::Disk
