#include "loose.h"
#include "disk.h"

#include <util/file.h>

#include <contrib/lz4/lz4.h>
#include <contrib/xxhash/xxhash.h>

#include <cstddef>
#include <limits>

namespace Vcs::Store {

static std::filesystem::path MakePath(const std::filesystem::path& root, const HashId& id) {
    const auto hex = id.ToHex();
    return root / hex.substr(0, 2) / hex;
}

Loose::Loose(std::filesystem::path path, const Options& options)
    : path_(std::move(path))
    , options_(options) {
    // Force directory existence.
    std::filesystem::create_directories(path_);
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

DataHeader Loose::GetMeta(const HashId& id) const try
{
    auto file = File::ForRead(MakePath(path_, id));
    Disk::LooseHeader hdr{};
    // Read file header.
    if (file.Load(&hdr, sizeof(hdr)) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }
    return DataHeader::Make(hdr.Type(), hdr.Size());

} catch (const std::system_error& e) {
    if (std::errc::file_exists == e.code()) {
        return DataHeader();
    }
    throw;
}

bool Loose::Exists(const HashId& id) const {
    return std::filesystem::exists(MakePath(path_, id));
}

Object Loose::Load(const HashId& id, const DataType expected) const try
{
    auto file = File::ForRead(MakePath(path_, id));
    Disk::LooseHeader hdr{};
    // Read file header.
    if (file.Load(&hdr, sizeof(hdr)) != sizeof(hdr)) {
        throw std::runtime_error("cannot read file header");
    }
    // Validate data integrity.
    if (hdr.crc != XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0)) {
        throw std::runtime_error("header data corruption");
    }
    // Type mismatch.
    if (expected != DataType::None && hdr.Type() != expected && hdr.Type() != DataType::Index) {
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
                auto comp = std::make_unique_for_overwrite<char[]>(hdr.stored);

                read_to_buffer(reinterpret_cast<std::byte*>(comp.get()), hdr.stored);

                const int ret = ::LZ4_decompress_safe(comp.get(), (char*)buf, hdr.stored, buf_len);
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

void Loose::Put(const HashId& id, const DataType type, const std::string_view content) {
    if (content.size() > Disk::kMaximumContentSize) {
        throw std::length_error(fmt::format("object size exceed {} bytes", Disk::kMaximumContentSize));
    }

    std::filesystem::create_directories(path_ / id.ToHex().substr(0, 2));
    auto file = File::ForOverwrite(MakePath(path_, id));
    Disk::LooseHeader hdr{};
    hdr.tag = Disk::LooseHeader::MakeTag(options_.codec, type);
    hdr.original = content.size();

    const auto write_to_file = [&](const void* buf, size_t buf_len) {
        const uint64_t content_crc = XXH3_64bits(buf, buf_len);
        // Setup length of stored data.
        hdr.stored = buf_len;
        // Setup header checksum.
        hdr.crc = XXH32(&hdr, offsetof(Disk::LooseHeader, crc), 0);
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
            auto buf_size = ::LZ4_compressBound(content.size());
            auto buf = std::make_unique_for_overwrite<char[]>(buf_size);

            int len = ::LZ4_compress_default(content.data(), buf.get(), content.size(), buf_size);

            if (len == 0) {
                throw std::runtime_error("cannot compress data");
            }

            write_to_file(buf.get(), len);
            break;
        }
    }

    // Flush written data if required.
    if (options_.data_sync) {
        file.FlushData();
    }
}

} // namespace Vcs::Store
