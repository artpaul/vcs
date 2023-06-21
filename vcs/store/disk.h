#pragma once

#include <vcs/object/data.h>
#include <vcs/object/hashid.h>

#include <util/varint.h>

#include <limits>

namespace Vcs::Store::Disk {

/// Size of written objects should not exceed 134'217'727 bytes.
static constexpr size_t kMaximumContentSize = (128u << 20) - 1u;

/**
 * Header entry for loose and pack storage.
 */
struct alignas(4) LooseHeader {
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

/// Ensure LooseHeader has expected size.
static_assert(sizeof(LooseHeader) == 16);
/// Ensure the crc field is placed last.
static_assert(offsetof(LooseHeader, crc) + sizeof(LooseHeader::crc) == sizeof(LooseHeader));
/// Ensure LooseHeader is trivially copyable.
static_assert(std::is_trivially_copyable_v<LooseHeader>);

static_assert(std::numeric_limits<decltype(LooseHeader::original)>::max() >= kMaximumContentSize);

/**
 * Index entry for pack storage.
 */
struct alignas(1) IndexTag {
    /// Packed header and offset.
    std::byte tag[12];

    IndexTag(const DataHeader hdr, const uint64_t offset) {
        // Set header.
        std::memcpy(tag, hdr.Data(), hdr.Bytes());
        // Set offset.
        if (!EncodeVarint(offset, reinterpret_cast<uint8_t*>(tag) + hdr.Bytes(), sizeof(tag) - hdr.Bytes()))
        {
            throw std::runtime_error("cannot pack offset");
        }
    }

    /** Object's header.*/
    DataHeader Meta() const noexcept {
        DataHeader hdr;
        std::memcpy(&hdr, tag, sizeof(hdr));
        return DataHeader::Make(hdr.Type(), hdr.Size());
    }

    /** Offset in data file to the beginning of object's record. */
    uint64_t Offset() const noexcept {
        DataHeader hdr;
        uint64_t value;
        std::memcpy(&hdr, tag, sizeof(hdr));
        DecodeVarint(reinterpret_cast<const uint8_t*>(tag) + hdr.Bytes(), sizeof(tag) - hdr.Bytes(), value);
        return value;
    }
};

/// Ensure IndexTag has expected size.
static_assert(sizeof(IndexTag) == 12);
/// Ensure IndexTag is 8-bit aligned.
static_assert(std::alignment_of<IndexTag>::value == std::alignment_of<std::byte>::value);
/// Ensure IndexTag is trivially copyable.
static_assert(std::is_trivially_copyable_v<IndexTag>);

/**
 * Tag for records in pack file.
 */
struct alignas(1) DataTag {
    // |----------------------------------------------------------------|
    // |                  The layout of data field                      |
    // |---------------->------------------|--------------<-------------|
    // | compressed : 1 | delta : 1  | : 3 |         size : 27          |
    // |----------------|------------|-----|----------------------------|

    uint8_t data[4];

public:
    constexpr DataTag() noexcept = default;

    constexpr DataTag(const uint32_t size, bool compressed, bool delta) noexcept {
        data[0] = ((size >> 24) & 0x07) | (compressed ? 0x80u : 0u) | (delta ? 0x40u : 0u);
        data[1] = ((size >> 16) & 0xFF);
        data[2] = ((size >> 8) & 0xFF);
        data[3] = ((size >> 0) & 0xFF);
    }

public:
    /** The content is compressed. */
    constexpr bool IsCompressed() const noexcept {
        return (data[0] & 0x80) != 0;
    }

    /** The content is delta. */
    constexpr bool IsDelta() const noexcept {
        return (data[0] & 0x40) != 0;
    }

    /** Length of the content. */
    constexpr uint32_t Length() const noexcept {
        return uint32_t((data[3])) | uint32_t((data[2]) << 8) | uint32_t((data[1]) << 16)
             | uint32_t((data[0] & 0x07) << 24);
    }
};

/// Ensure DataTag has expected size.
static_assert(sizeof(DataTag) == 4);
/// Ensure DataTag is trivially copyable.
static_assert(std::is_trivially_copyable_v<DataTag>);

} // namespace Vcs::Store::Disk
