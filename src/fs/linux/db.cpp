#include "db.h"

#include <contrib/fmt/fmt/format.h>

namespace Vcs::Fs {

using leveldb::ReadOptions;
using leveldb::Slice;
using leveldb::WriteOptions;

Metabase::Metabase(const std::string& path)
    : db_(nullptr) {
    leveldb::Options options;

    options.create_if_missing = true;

    auto status = leveldb::DB::Open(options, path, &db_);

    if (!status.ok()) {
        throw std::runtime_error(fmt::format("cannot open metabase '{}'", status.ToString()));
    }
}

Metabase::~Metabase() {
    delete db_;
}

auto Metabase::GetMetadata(const std::string_view path) const -> std::optional<Value> {
    std::string value;

    const auto status = db_->Get(ReadOptions(), Slice(path.data(), path.size()), &value);
    if (!status.ok()) {
        return {};
    }

    // An empty value represents a tombstone marker.
    if (value.empty()) {
        return std::make_optional<Value>();
    }
    // The value stores just timestamps.
    if (value.size() == sizeof(Timestamps)) {
        Timestamps ts;
        // Copy the value.
        std::memcpy(&ts, value.data(), value.size());

        return std::optional<Value>(std::in_place_t(), ts);
    }
    // The value stores the full metadata.
    if (value.size() == sizeof(Meta)) {
        Meta meta;
        // Copy the value.
        std::memcpy(&meta, value.data(), value.size());

        return std::optional<Value>(std::in_place_t(), meta);
    }
    // Unsuported size of the value.
    return {};
}

bool Metabase::PutTimestamps(const std::string_view path, const Timestamps& ts) {
    return db_->Put(WriteOptions(), Slice(path.data(), path.size()), Slice((const char*)&ts, sizeof(ts)))
        .ok();
}

} // namespace Vcs::Fs
