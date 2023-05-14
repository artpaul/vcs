#pragma once

#include <filesystem>
#include <memory>
#include <optional>

namespace Vcs {

class KeyValueDatabase {
public:
    KeyValueDatabase(const std::filesystem::path& path);
    ~KeyValueDatabase();

    void Delete(const std::string_view key);

    std::optional<std::string> Get(const std::string_view key) const;

    void Put(const std::string_view key, const std::string_view value);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

template <typename T>
class Database {
public:
    Database(const std::filesystem::path& path)
        : db_(path) {
    }

    void Delete(const std::string_view key) {
        db_.Delete(key);
    }

    std::optional<T> Get(const std::string_view key) const {
        if (const auto& value = db_.Get(key)) {
            return T::Load(*value);
        }
        return std::nullopt;
    }

    void Put(const std::string_view key, const T& rec) {
        db_.Put(key, T::Save(rec));
    }

private:
    KeyValueDatabase db_;
};

} // namespace Vcs
