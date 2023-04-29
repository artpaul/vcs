#include "memory.h"

namespace Vcs::Store {

MemoryCache::MemoryCache(size_t capacity, size_t chunk_size)
    : Datastore(chunk_size)
    , capacity_(capacity)
    , size_(0) {
}

size_t MemoryCache::Size() const noexcept {
    return size_;
}

DataHeader MemoryCache::DoGetMeta(const HashId& id) const {
    std::lock_guard g(mutex_);

    if (auto oi = objects_.find(id); oi != objects_.end()) {
        const Object& obj = oi->second->second;

        return DataHeader::Make(obj.Type(), obj.Size());
    } else {
        return DataHeader();
    }
}

bool MemoryCache::DoIsExists(const HashId& id) const {
    std::lock_guard g(mutex_);

    return objects_.find(id) != objects_.end();
}

Object MemoryCache::DoLoad(const HashId& id, const DataType) const {
    std::lock_guard g(mutex_);

    if (auto oi = objects_.find(id); oi != objects_.end()) {
        list_.splice(list_.end(), list_, oi->second);
        return oi->second->second;
    } else {
        return Object();
    }
}

HashId MemoryCache::DoPut(DataType type, std::string_view content) {
    return InsertObject(HashId::Make(type, content), Object::Load(type, content));
}

HashId MemoryCache::InsertObject(const HashId& id, Object obj) {
    UsageList removed;

    {
        std::lock_guard g(mutex_);
        auto oi = objects_.emplace(id, list_.end());
        if (oi.second) {
            size_ += obj.Size();
            // Replace iterator with the real value.
            oi.first->second = list_.insert(list_.end(), std::make_pair(id, obj));
        }
        // Free memory.
        while (!list_.empty() && size_ > capacity_) {
            size_ -= list_.front().second.Size();
            // Remove object from map.
            objects_.erase(list_.front().first);
            // Keep references to the objects so they can be freed outside the critical section.
            removed.splice(removed.end(), list_, list_.begin());
        }
    }

    return id;
}

} // namespace Vcs::Store
