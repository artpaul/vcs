#pragma once

#include <vcs/object/store.h>

#include <list>
#include <mutex>
#include <unordered_map>

namespace Vcs {

class MemoryStore : public Datastore {
public:
    explicit MemoryStore(size_t capacity = 64u << 20, size_t chunk_size = 4u << 20);

    size_t Size() const noexcept;

protected:
    DataHeader DoGetMeta(const HashId& id) const override;

    bool DoIsExists(const HashId& id) const override;

    Object DoLoad(const HashId& id, const DataType expected) const override;

    HashId DoPut(DataType type, std::string_view content) override;

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

} // namespace Vcs
