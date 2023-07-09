#pragma once

#include "meta.h"

#include <contrib/leveldb/include/leveldb/db.h>

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

namespace Vcs::Fs {

class Metabase {
public:
    using Value = std::variant<std::monostate, Timestamps, Meta>;

    explicit Metabase(const std::string& path);

    ~Metabase();

    std::optional<Value> GetMetadata(const std::string_view path) const;

    bool PutMeta(const std::string_view path, const Meta& meta);

    bool PutTimestamps(const std::string_view path, const Timestamps& ts);

private:
    leveldb::DB* db_;
};

} // namespace Vcs::Fs
