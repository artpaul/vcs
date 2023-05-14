#pragma once

#include <vcs/object/store.h>

#include <vector>

namespace Vcs::Store {

/**
 * Collects ids and types of stored objects.
 */
class Collect : public Datastore::Backend {
public:
    template <typename... Args>
    static auto Make(Args&&... args) {
        return std::make_shared<Collect>(std::forward<Args>(args)...);
    }

    const std::vector<std::pair<HashId, DataType>>& GetIds() const& {
        return oids_;
    }

    std::vector<std::pair<HashId, DataType>> GetIds() const&& {
        return std::move(oids_);
    }

private:
    DataHeader GetMeta(const HashId&) const override {
        return DataHeader();
    }

    bool Exists(const HashId&) const override {
        return false;
    }

    Object Load(const HashId&, const DataType) const override {
        return Object();
    }

    void Put(const HashId& id, DataType type, std::string_view) override {
        oids_.emplace_back(id, type);
    }

private:
    std::vector<std::pair<HashId, DataType>> oids_;
};

} // namespace Vcs::Store
