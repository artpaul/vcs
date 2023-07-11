#include "db.h"

#include <contrib/fmt/fmt/format.h>
#include <contrib/leveldb/include/leveldb/write_batch.h>

namespace Vcs::Fs {

using leveldb::Iterator;
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

void Metabase::Delete(const std::vector<std::string>& keys) {
    leveldb::WriteBatch batch;

    if (keys.empty()) {
        return;
    }
    for (const auto& key : keys) {
        batch.Delete(Slice(key.data(), key.size()));
    }

    db_->Write(WriteOptions(), &batch);
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

void Metabase::Enumerate(const std::function<void(std::string_view, const Value&)>& on_record) const {
    auto snapshot = db_->GetSnapshot();

    try {
        std::unique_ptr<Iterator> it(db_->NewIterator(ReadOptions{.snapshot = snapshot}));

        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            const auto key = it->key();
            const auto value = it->value();

            // An empty value represents a tombstone marker.
            if (value.empty()) {
                on_record(std::string_view(key.data(), key.size()), Value());
            }
            // The value stores just timestamps.
            if (value.size() == sizeof(Timestamps)) {
                Timestamps ts;
                // Copy the value.
                std::memcpy(&ts, value.data(), value.size());

                on_record(std::string_view(key.data(), key.size()), ts);
            }
            // The value stores the full metadata.
            if (value.size() == sizeof(Meta)) {
                Meta meta;
                // Copy the value.
                std::memcpy(&meta, value.data(), value.size());

                on_record(std::string_view(key.data(), key.size()), meta);
            }
        }

        db_->ReleaseSnapshot(snapshot);
    } catch (...) {
        db_->ReleaseSnapshot(snapshot);
        throw;
    }
}

bool Metabase::PutMeta(const std::string_view path, const Meta& meta) {
    return db_
        ->Put(WriteOptions(), Slice(path.data(), path.size()), Slice((const char*)&meta, sizeof(meta)))
        .ok();
}

bool Metabase::PutTimestamps(const std::string_view path, const Timestamps& ts) {
    return db_->Put(WriteOptions(), Slice(path.data(), path.size()), Slice((const char*)&ts, sizeof(ts)))
        .ok();
}

} // namespace Vcs::Fs
