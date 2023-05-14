#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace Vcs {

class KeyValueDatabase {
public:
    KeyValueDatabase(const std::filesystem::path& path);
    ~KeyValueDatabase();

    void Delete(const std::string_view key);

    void Enumerate(const std::function<bool(const std::string_view, const std::string_view)>& cb) const;

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

    void Enumerate(const std::function<bool(const std::string_view, const T&)>& cb) const {
        db_.Enumerate([&cb](const std::string_view key, const std::string_view value) {
            return cb(key, T::Load(value));
        });
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
