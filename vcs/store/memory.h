#pragma once

#include <vcs/object/store.h>

#include <list>
#include <mutex>
#include <unordered_map>

namespace Vcs::Store {

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
    HashId InsertObject(const HashId& id, Object obj);

private:
    using UsageList = std::list<std::pair<HashId, Object>>;

    size_t capacity_;
    size_t size_;
    mutable std::mutex mutex_;
    mutable UsageList list_;
    std::unordered_map<HashId, UsageList::iterator> objects_;
};

} // namespace Vcs::Store
