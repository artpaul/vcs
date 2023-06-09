#include "memory.h"

namespace Vcs::Store {

MemoryCache::MemoryCache(size_t capacity)
    : capacity_(capacity)
    , size_(0) {
}

size_t MemoryCache::Size() const noexcept {
    return size_;
}

DataHeader MemoryCache::GetMeta(const HashId& id) const {
    if (auto oi = objects_.find(id); oi != objects_.end()) {
        const Object& obj = oi->second->second;

        return DataHeader::Make(obj.Type(), obj.Size());
    } else {
        return DataHeader();
    }
}

bool MemoryCache::Exists(const HashId& id) const {
    return objects_.contains(id);
}

Object MemoryCache::Load(const HashId& id, const DataType expected) const {
    if (auto oi = objects_.find(id); oi != objects_.end()) {
        // Type mismatch.
        if ((expected != DataType::None) && (oi->second->second.Type() != expected)
            && (oi->second->second.Type() != DataType::Index))
        {
            return Object();
        }
        list_.splice(list_.end(), list_, oi->second);
        return oi->second->second;
    } else {
        return Object();
    }
}

void MemoryCache::Put(const HashId& id, DataType type, std::string_view content) {
    InsertObject(id, Object::Load(type, content));
}

void MemoryCache::Put(const HashId& id, const Object& obj) {
    InsertObject(id, obj);
}

void MemoryCache::InsertObject(const HashId& id, Object obj) {
    if (auto [oi, inserted] = objects_.emplace(id, list_.end()); inserted) {
        size_ += obj.Size();
        // Replace iterator with the real value.
        oi->second = list_.insert(list_.end(), std::make_pair(id, std::move(obj)));
    } else {
        return;
    }
    // Free memory.
    while (!list_.empty() && size_ > capacity_) {
        size_ -= list_.front().second.Size();
        // Remove object from map.
        objects_.erase(list_.front().first);
        // Remove iterator from the list.
        list_.pop_front();
    }
}

} // namespace Vcs::Store
