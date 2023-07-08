#pragma once

#include "meta.h"

#include <contrib/leveldb/include/leveldb/db.h>

#include <memory>
#include <optional>
#include <string_view>

namespace Vcs::Fs {

class Metabase {
public:
    explicit Metabase(const std::string& path);

    ~Metabase();

    std::optional<Meta> GetMetadata(const std::string_view path) const;

    bool PutTimestamps(const std::string_view path, const Timestamps& ts);

private:
    leveldb::DB* db_;
};

} // namespace Vcs::Fs
