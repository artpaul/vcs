#include "loose.h"

#include <vcs/util/file.h>

#include <contrib/lz4/lz4.h>
#include <contrib/xxhash/xxhash.h>

#include <cstddef>
#include <limits>

namespace Vcs::Store {
namespace {

/// Size of written objects should not exceed 2'113'929'216 bytes.
constexpr size_t kMaximumContentSize = LZ4_MAX_INPUT_SIZE;

struct FileHeader {
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

// Ensure FileHeader has expected size.
static_assert(sizeof(FileHeader) == 16);
// Ensure the crc field is placed last.
static_assert(offsetof(FileHeader, crc) + sizeof(FileHeader::crc) == sizeof(FileHeader));

static_assert(std::numeric_limits<decltype(FileHeader::original)>::max() >= kMaximumContentSize);

std::filesystem::path MakePath(const std::filesystem::path& root, const HashId& id) {
    const auto hex = id.ToHex();
    return root / hex.substr(0, 2) / hex;
}

} // namespace

Loose::Loose(const std::filesystem::path& path, const Options& options)
    : path_(path)
    , options_(options) {
    // Force directory existence.
    std::filesystem::create_directories(path);
}

void Loose::Enumerate(bool with_metadata, const std::function<bool(const HashId&, const DataHeader)>& cb)
    const {
    // Do not read any data if callback is empty.
    if (!bool(cb)) {
        return;
    }

    for (auto di = std::filesystem::recursive_directory_iterator(path_); const auto& entry : di) {
        const auto& filename = entry.path().filename();

        if (entry.is_directory()) {
            if (di.depth() != 0 || filename.string().size() != 2) {
                di.disable_recursion_pending();
            }
        } else if (entry.is_regular_file()) {
            if (di.depth() != 1 || !HashId::IsHex(filename.string())) {
                continue;
            }

            (void)with_metadata; // TODO: non throwing integrity check.

            if (!cb(HashId::FromHex(filename.string()), DataHeader())) {
                break;
            }
        }
    }
}

DataHeader Loose::DoGetMeta(const HashId& id) const try
{
    auto file = File::ForRead(MakePath(path_, id));
    FileHeader hdr{};
    // Read file header.
    if (file.Load(&hdr, sizeof(hdr)) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(FileHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }
    return DataHeader::Make(hdr.Type(), hdr.Size());
} catch (const std::system_error& e) {
    if (std::errc::file_exists == e.code()) {
        return DataHeader();
    }
    throw;
}

bool Loose::DoIsExists(const HashId& id) const {
    return std::filesystem::exists(MakePath(path_, id));
}

Object Loose::DoLoad(const HashId& id, const DataType expected) const try
{
    auto file = File::ForRead(MakePath(path_, id));
    FileHeader hdr{};
    // Read file header.
    if (file.Load(&hdr, sizeof(hdr)) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(FileHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }
    // Type mismatch.
    if (expected != DataType::None && hdr.Type() != expected) {
        return Object();
    }
    // Load object content.
    return Object::Load(DataHeader::Make(hdr.Type(), hdr.Size()), [&](std::byte* buf, size_t buf_len) {
        const auto read_to_buffer = [&file](std::byte* p, size_t size) {
            uint64_t content_crc;
            // Load content.
            if (file.Load(p, size) != size) {
                throw std::runtime_error("unexpected end of stream");
            }
            // Load checksum.
            if (file.Load(&content_crc, sizeof(content_crc)) != sizeof(content_crc)) {
                throw std::runtime_error("unexpected end of stream");
            }
            // Compare checksums.
            if (content_crc != XXH3_64bits(p, size)) {
                throw std::runtime_error("content data corruption");
            }
        };

        switch (hdr.Codec()) {
            case Compression::None: {
                read_to_buffer(buf, buf_len);
                break;
            }
            case Compression::Lz4: {
                std::vector<char> comp(hdr.stored);

                read_to_buffer(reinterpret_cast<std::byte*>(comp.data()), comp.size());

                const int ret = ::LZ4_decompress_safe(comp.data(), (char*)buf, comp.size(), buf_len);
                if (ret != int(buf_len)) {
                    throw std::runtime_error(fmt::format("cannot decompres content '{}'", ret));
                }
                break;
            }
        }
    });
} catch (const std::system_error& e) {
    if (std::errc::file_exists == e.code()) {
        return Object();
    }
    throw;
}

HashId Loose::DoPut(const DataType type, const std::string_view content) {
    if (content.size() > kMaximumContentSize) {
        throw std::length_error(fmt::format("object size exceed {} bytes", kMaximumContentSize));
    }

    auto id = HashId::Make(type, content);
    std::filesystem::create_directories(path_ / id.ToHex().substr(0, 2));
    auto file = File::ForOverwrite(MakePath(path_, id));
    FileHeader hdr{};
    hdr.tag = FileHeader::MakeTag(options_.codec, type);
    hdr.original = content.size();

    const auto write_to_file = [&](const void* buf, size_t buf_len) {
        const uint64_t content_crc = XXH3_64bits(buf, buf_len);
        // Setup length of stored data.
        hdr.stored = buf_len;
        // Setup header checksum.
        hdr.crc = XXH32(&hdr, offsetof(FileHeader, crc), 0);
        // Write file header.
        file.Write(&hdr, sizeof(hdr));
        // Write file content.
        file.Write(buf, buf_len);
        // Write content checksum.
        file.Write(&content_crc, sizeof(content_crc));
    };

    switch (options_.codec) {
        case Compression::None: {
            write_to_file(content.data(), content.size());
            break;
        }
        case Compression::Lz4: {
            std::vector<char> buf(::LZ4_compressBound(content.size()));

            int len = ::LZ4_compress_default(content.data(), buf.data(), content.size(), buf.size());

            if (len == 0) {
                throw std::runtime_error("cannot compress data");
            }

            write_to_file(buf.data(), len);
            break;
        }
    }

    // Flush written data if required.
    if (options_.data_sync) {
        file.FlushData();
    }

    return id;
}

} // namespace Vcs::Store
