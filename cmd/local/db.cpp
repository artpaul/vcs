#include "db.h"

#include <contrib/fmt/fmt/format.h>
#include <contrib/liblmdb/lmdb.h>

namespace Vcs {

class KeyValueDatabase::Impl {
public:
    Impl(const std::filesystem::path& path)
        : env_(nullptr) {
        Check(mdb_env_create(&env_));
        Check(mdb_env_set_mapsize(env_, 1ull << 30));
        Check(mdb_env_open(env_, path.c_str(), 0, 0664));
    }

    ~Impl() {
        if (env_) {
            mdb_env_close(env_);
        }
    }

    void Delete(const std::string_view key) {
        (void)key;
    }

    void Enumerate(const std::function<bool(const std::string_view, const std::string_view)>& cb) const {
        MDB_dbi dbi;
        MDB_txn* txn = nullptr;
        MDB_cursor* cursor = nullptr;
        MDB_val key;
        MDB_val data;
        int rc;

        Check(mdb_txn_begin(env_, NULL, MDB_RDONLY, &txn));
        Check(mdb_dbi_open(txn, NULL, 0, &dbi));
        Check(mdb_cursor_open(txn, dbi, &cursor));
        while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
            if (!cb(std::string_view((const char*)key.mv_data, key.mv_size),
                    std::string_view((const char*)data.mv_data, data.mv_size)))
            {
                break;
            }
        }
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        mdb_dbi_close(env_, dbi);
    }

    std::optional<std::string> Get(const std::string_view k) const {
        MDB_dbi dbi;
        MDB_txn* txn = nullptr;

        Check(mdb_txn_begin(env_, NULL, MDB_RDONLY, &txn));
        Check(mdb_dbi_open(txn, NULL, 0, &dbi));

        MDB_val key;
        MDB_val val;

        key.mv_data = (void*)k.data();
        key.mv_size = k.size();
        int rc = mdb_get(txn, dbi, &key, &val);
        if (rc != 0) {
            mdb_txn_abort(txn);
            mdb_dbi_close(env_, dbi);

            if (rc == MDB_NOTFOUND) {
                return std::nullopt;
            }

            throw std::runtime_error(fmt::format("cannot get data: {}", mdb_strerror(rc)));
        }

        auto result = std::make_optional<std::string>((const char*)val.mv_data, val.mv_size);

        mdb_txn_abort(txn);
        mdb_dbi_close(env_, dbi);

        return result;
    }

    void Put(const std::string_view k, const std::string_view value) {
        MDB_dbi dbi;
        MDB_txn* txn = nullptr;
        Check(mdb_txn_begin(env_, nullptr, 0, &txn));
        Check(mdb_dbi_open(txn, nullptr, 0, &dbi));

        MDB_val key;
        MDB_val val;
        key.mv_size = k.size();
        key.mv_data = (void*)k.data();
        val.mv_size = value.size();
        val.mv_data = (void*)value.data();

        int rc = mdb_put(txn, dbi, &key, &val, 0);
        if (rc != 0) {
            mdb_dbi_close(env_, dbi);
            throw std::runtime_error(fmt::format("cannot put data: {}", mdb_strerror(rc)));
        }
        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            mdb_dbi_close(env_, dbi);
            throw std::runtime_error(fmt::format("cannot commit: {}", mdb_strerror(rc)));
        }

        mdb_dbi_close(env_, dbi);
    }

private:
    static void Check(const int rc) {
        if (rc != MDB_SUCCESS) {
            throw std::runtime_error(fmt::format("db error: {}", mdb_strerror(rc)));
        }
    }

private:
    MDB_env* env_;
};

KeyValueDatabase::KeyValueDatabase(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {
}

KeyValueDatabase::~KeyValueDatabase() {
}

void KeyValueDatabase::Delete(const std::string_view key) {
    impl_->Delete(key);
}

void KeyValueDatabase::Enumerate(
    const std::function<bool(const std::string_view, const std::string_view)>& cb
) const {
    return impl_->Enumerate(cb);
}

auto KeyValueDatabase::Get(const std::string_view key) const -> std::optional<std::string> {
    return impl_->Get(key);
}

void KeyValueDatabase::Put(const std::string_view key, const std::string_view value) {
    impl_->Put(key, value);
}

} // namespace Vcs
