#pragma once

#include "meta.h"

#include <contrib/leveldb/include/leveldb/db.h>

#include <functional>
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

    void Delete(const std::vector<std::string>& keys);

    std::optional<Value> GetMetadata(const std::string_view path) const;

    void Enumerate(const std::function<void(std::string_view, const Value&)>& on_record) const;

    bool PutDelete(const std::string_view path);

    bool PutMeta(const std::string_view path, const Meta& meta);

    bool PutTimestamps(const std::string_view path, const Timestamps& ts);

private:
    leveldb::DB* db_;
};

} // namespace Vcs::Fs
