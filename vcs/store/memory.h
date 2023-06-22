#pragma once

#include <vcs/object/store.h>

#include <absl/container/flat_hash_map.h>
#include <list>

namespace Vcs::Store {

/**
 * @note The cache is not thread-safe.
 */
class MemoryCache : public Datastore::Backend {
public:
    explicit MemoryCache(size_t capacity = 64u << 20);

    template <typename... Args>
    static auto Make(Args&&... args) {
        return std::make_shared<MemoryCache>(std::forward<Args>(args)...);
    }

    size_t Size() const noexcept;

protected:
    DataHeader GetMeta(const HashId& id) const override;

    bool Exists(const HashId& id) const override;

    Object Load(const HashId& id, const DataType expected) const override;

    void Put(const HashId& id, DataType type, std::string_view content) override;

    void Put(const HashId& id, const Object& obj) override;

private:
    void InsertObject(const HashId& id, Object obj);

private:
    using UsageList = std::list<std::pair<HashId, Object>>;

    size_t capacity_;
    size_t size_;
    mutable UsageList list_;
    absl::flat_hash_map<HashId, UsageList::iterator> objects_;
};

} // namespace Vcs::Store
