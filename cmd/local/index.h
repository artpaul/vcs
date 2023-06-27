#pragma once

#include "db.h"

#include <filesystem>

namespace Vcs {

class TreeIndex {
public:
    TreeIndex(const std::filesystem::path& path, const Lmdb::Options& options);

    void Start();

    std::expected<std::string_view, Lmdb::Status> Get(const std::string_view key) const;

    void Update(std::string key, std::string value);

    void Flush();

private:
    /// Index database.
    std::unique_ptr<Lmdb::Database> db_;
    /// Reader transaction.
    std::unique_ptr<Lmdb::Database::Transaction> reader_;
    /// New values to set.
    std::vector<std::pair<std::string, std::string>> updates_;
};

} // namespace Vcs
