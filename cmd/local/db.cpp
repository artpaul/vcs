#include "db.h"

#include <contrib/fmt/fmt/format.h>
#include <contrib/liblmdb/lmdb.h>

namespace Vcs {
namespace Lmdb {

std::string_view Status::Message() const noexcept {
    switch (code_) {
        case Code::Success:
            return "OK";
        case Code::NotFound:
            return "Not found";
        case Code::IOError:
            return ::mdb_strerror(error_);
    }
    return {};
}

class Database::Impl {
public:
    Impl(const std::filesystem::path& path, const Options& options);

    ~Impl() noexcept;

    Status Delete(const std::string_view key) noexcept;

    Status Enumerate(const std::function<bool(const std::string_view, const std::string_view)>& cb) const;

    std::expected<std::string, Status> Get(const std::string_view key) const;

    Status Put(const std::string_view key, const std::string_view value) noexcept;

private:
    struct DBI {
        MDB_env* env;
        MDB_dbi dbi;

        explicit constexpr DBI(MDB_env* e) noexcept
            : env(e)
            , dbi(0) {
        }

        ~DBI() noexcept {
            if (dbi) {
                ::mdb_dbi_close(env, dbi);
            }
        }
    };

    struct TXN {
        MDB_txn* txn{nullptr};

        ~TXN() noexcept {
            if (txn) {
                ::mdb_txn_abort(txn);
            }
        }
    };

private:
    MDB_env* env_;
};

Database::Impl::Impl(const std::filesystem::path& path, const Options& options)
    : env_(nullptr) {
    if (options.create_if_missing) {
        std::filesystem::create_directories(path);
    }

    const auto check = [](const int rc) {
        if (rc != MDB_SUCCESS) {
            throw std::runtime_error(fmt::format("db error: {}", ::mdb_strerror(rc)));
        }
    };

    check(::mdb_env_create(&env_));
    check(::mdb_env_set_mapsize(env_, options.database_capacity));
    check(::mdb_env_open(env_, path.c_str(), MDB_NOTLS, 0664));
}

Database::Impl::~Impl() noexcept {
    if (env_) {
        ::mdb_env_close(env_);
    }
}

Status Database::Impl::Delete(const std::string_view k) noexcept {
    DBI dbi(env_);
    TXN txn;
    int ret = 0;

    if ((ret = ::mdb_txn_begin(env_, nullptr, 0, &txn.txn)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }
    if ((ret = ::mdb_dbi_open(txn.txn, nullptr, 0, &dbi.dbi)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }

    MDB_val key;
    key.mv_size = k.size();
    key.mv_data = (void*)k.data();

    if ((ret = ::mdb_del(txn.txn, dbi.dbi, &key, nullptr)) != MDB_SUCCESS) {
        if (ret == MDB_NOTFOUND) {
            return Status::NotFound();
        } else {
            return Status::IOError(ret);
        }
    }

    if ((ret = ::mdb_txn_commit(txn.txn)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    } else {
        txn.txn = nullptr;
    }

    return Status::Success();
}

Lmdb::Status Database::Impl::Enumerate(
    const std::function<bool(const std::string_view, const std::string_view)>& cb
) const {
    DBI dbi(env_);
    TXN txn;
    MDB_cursor* cursor = nullptr;
    MDB_val key;
    MDB_val data;
    int ret;

    if ((ret = ::mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn.txn)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }
    if ((ret = ::mdb_dbi_open(txn.txn, nullptr, 0, &dbi.dbi)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }
    if ((ret = ::mdb_cursor_open(txn.txn, dbi.dbi, &cursor)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }

    while ((ret = ::mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == MDB_SUCCESS) {
        try {
            if (!cb(std::string_view((const char*)key.mv_data, key.mv_size),
                    std::string_view((const char*)data.mv_data, data.mv_size)))
            {
                break;
            }
        } catch (...) {
            ::mdb_cursor_close(cursor);
            throw;
        }
    }

    ::mdb_cursor_close(cursor);

    return Status::Success();
}

std::expected<std::string, Status> Database::Impl::Get(const std::string_view k) const {
    DBI dbi(env_);
    TXN txn;
    int ret = 0;

    if ((ret = ::mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn.txn)) != MDB_SUCCESS) {
        return std::unexpected(Status::IOError(ret));
    }
    if ((ret = ::mdb_dbi_open(txn.txn, nullptr, 0, &dbi.dbi)) != MDB_SUCCESS) {
        return std::unexpected(Status::IOError(ret));
    }

    MDB_val key;
    MDB_val val;

    key.mv_data = (void*)k.data();
    key.mv_size = k.size();
    if ((ret = ::mdb_get(txn.txn, dbi.dbi, &key, &val)) != 0) {
        if (ret == MDB_NOTFOUND) {
            return std::unexpected(Status::NotFound());
        } else {
            return std::unexpected(Status::IOError(ret));
        }
    }

    return std::expected<std::string, Status>(std::in_place_t(), (const char*)val.mv_data, val.mv_size);
}

Status Database::Impl::Put(const std::string_view k, const std::string_view value) noexcept {
    DBI dbi(env_);
    TXN txn;
    int ret = 0;

    if ((ret = ::mdb_txn_begin(env_, nullptr, 0, &txn.txn)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }
    if ((ret = ::mdb_dbi_open(txn.txn, nullptr, 0, &dbi.dbi)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }

    MDB_val key;
    MDB_val val;
    key.mv_size = k.size();
    key.mv_data = (void*)k.data();
    val.mv_size = value.size();
    val.mv_data = (void*)value.data();

    // TODO: MDB_NOOVERWRITE
    if ((ret = ::mdb_put(txn.txn, dbi.dbi, &key, &val, 0)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    }
    if ((ret = ::mdb_txn_commit(txn.txn)) != MDB_SUCCESS) {
        return Status::IOError(ret);
    } else {
        txn.txn = nullptr;
    }

    return Status::Success();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Database::Database(const std::filesystem::path& path, const Options& options)
    : impl_(std::make_unique<Impl>(path, options)) {
}

Database::~Database() {
}

Status Database::Delete(const std::string_view key) {
    return impl_->Delete(key);
}

Lmdb::Status Database::Enumerate(
    const std::function<bool(const std::string_view, const std::string_view)>& cb
) const {
    return impl_->Enumerate(cb);
}

auto Database::Get(const std::string_view key) const -> std::expected<std::string, Status> {
    return impl_->Get(key);
}

Status Database::Put(const std::string_view key, const std::string_view value) {
    return impl_->Put(key, value);
}

} // namespace Lmdb
} // namespace Vcs
