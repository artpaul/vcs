#include "index.h"

namespace Vcs {

TreeIndex::TreeIndex(const std::filesystem::path& path, const Lmdb::Options& options) {
    db_ = std::make_unique<Lmdb::Database>(path, options);
}

void TreeIndex::Start() {
    reader_.reset();

    if (auto ret = db_->StartTransaction(true)) {
        reader_ = std::move(ret.value());
    }
}

std::expected<std::string_view, Lmdb::Status> TreeIndex::Get(const std::string_view key) const {
    if (reader_) {
        return reader_->Get(key);
    } else {
        return std::unexpected(Lmdb::Status::NotFound());
    }
}

void TreeIndex::Update(std::string key, std::string value) {
    updates_.emplace_back(std::move(key), std::move(value));
}

void TreeIndex::Flush() {
    if (updates_.empty()) {
        return;
    } else {
        reader_.reset();
    }

    std::sort(updates_.begin(), updates_.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    if (auto t = db_->StartTransaction(false)) {
        for (const auto& [key, value] : updates_) {
            if (!t.value()->Put(key, value)) {
                return;
            }
        }

        t.value()->Commit();
    }
}

} // namespace Vcs
