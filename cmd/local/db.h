#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>

namespace Vcs {
namespace Lmdb {

struct Options {
    size_t database_capacity = 1ull << 30;

    /// If true, the database will be created if it is missing.
    bool create_if_missing = false;

    /// If true, the database will be opened in read-only mode.
    bool read_only = false;
};

class Status {
    enum class Code {
        Success = 0,
        NotFound = 1,
        IOError = 5,
    };

public:
    constexpr Status() noexcept = default;

    static constexpr Status Success() noexcept {
        return Status();
    }

    static constexpr Status NotFound() noexcept {
        return Status(Code::NotFound);
    }

    static constexpr Status IOError(int error) noexcept {
        return Status(Code::IOError, error);
    }

    std::string_view Message() const noexcept;

public:
    /// Returns true if the status indicates success.
    constexpr bool IsIOError() const noexcept {
        return code_ == Code::IOError;
    }

    /// Returns true if the status indicates a NotFound error.
    constexpr bool IsNotFound() const noexcept {
        return code_ == Code::NotFound;
    }

    /// Returns true if the status indicates success.
    constexpr bool IsSuccess() const noexcept {
        return code_ == Code::Success;
    }

    explicit constexpr operator bool() const noexcept {
        return IsSuccess();
    }

private:
    constexpr Status(Code code, int error = 0) noexcept
        : code_(code)
        , error_(error) {
    }

private:
    Code code_{Code::Success};
    int error_{0};
};

class Database {
public:
    class Transaction {
    public:
        virtual ~Transaction() noexcept = default;

        /**
         * Commit changes.
         */
        virtual Status Commit() noexcept = 0;

        /**
         * Rollback changes.
         */
        virtual void Rollback() noexcept = 0;

    public:
        /**
         * Get value by key.
         */
        virtual std::expected<std::string_view, Status> Get(const std::string_view key) const = 0;

        /**
         * Put key-value pair.
         */
        virtual Status Put(const std::string_view key, const std::string_view value) = 0;
    };

public:
    Database(const std::filesystem::path& path, const Options& options);
    ~Database();

    std::expected<std::unique_ptr<Transaction>, Status> StartTransaction(bool read_only);

public:
    Status Delete(const std::string_view key);

    Status Enumerate(const std::function<bool(const std::string_view, const std::string_view)>& cb) const;

    std::expected<std::string, Status> Get(const std::string_view key) const;

    Status Put(const std::string_view key, const std::string_view value);

    /**
     * Overwrite the database with the list of items.
     *
     * @param items list of key-value pairs sorted by key.
     * @return Status
     */
    Status Reset(const std::vector<std::pair<std::string, std::string>>& items);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lmdb

template <typename T>
class Database {
public:
    explicit Database(const std::filesystem::path& path, const Lmdb::Options& options = Lmdb::Options())
        : db_(path, options) {
    }

    Lmdb::Status Delete(const std::string_view key) {
        return db_.Delete(key);
    }

    Lmdb::Status Enumerate(const std::function<bool(const std::string_view, const T&)>& cb) const {
        return db_.Enumerate([&cb](const std::string_view key, const std::string_view value) {
            return cb(key, T::Load(value));
        });
    }

    std::expected<T, Lmdb::Status> Get(const std::string_view key) const {
        if (auto value = db_.Get(key)) {
            return T::Load(*value);
        } else {
            return std::unexpected(value.error());
        }
    }

    Lmdb::Status Put(const std::string_view key, const T& rec) {
        return db_.Put(key, T::Save(rec));
    }

private:
    Lmdb::Database db_;
};

} // namespace Vcs
