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

std::optional<Meta> Metabase::GetMetadata(const std::string_view path) const {
    std::string value;

    const auto status = db_->Get(ReadOptions(), Slice(path.data(), path.size()), &value);
    if (!status.ok()) {
        return {};
    }

    auto result = std::make_optional<Meta>();
    // An empty value represents a tombstone marker.
    if (value.empty()) {
        return result;
    }
    // The value stores just timestamps.
    if (value.size() == sizeof(Timestamps)) {
        Timestamps ts;
        // Copy the value.
        std::memcpy(&ts, value.data(), value.size());

        result->ctime = ts.ctime;
        result->mtime = ts.mtime;

        return result;
    }
    // The value stores the full metadata.
    if (value.size() == sizeof(Meta)) {
        // Copy the value.
        std::memcpy(&(*result), value.data(), value.size());

        return result;
    }
    // Unsuported size of the value.
    return {};
}

bool Metabase::PutTimestamps(const std::string_view path, const Timestamps& ts) {
    return db_->Put(WriteOptions(), Slice(path.data(), path.size()), Slice((const char*)&ts, sizeof(ts)))
        .ok();
}

} // namespace Vcs::Fs
