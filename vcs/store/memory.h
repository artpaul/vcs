#pragma once

#include <vcs/object/store.h>

#include <absl/container/flat_hash_map.h>
#include <list>
#include <mutex>

namespace Vcs::Store {

class NoLock {
public:
    constexpr void lock() noexcept {
    }

    constexpr void unlock() noexcept {
    }

    constexpr bool try_lock() noexcept {
        return true;
    }
};

template <typename Mutex = std::mutex>
class MemoryCache : public Datastore::Backend {
public:
    explicit MemoryCache(size_t capacity = 64u << 20)
        : capacity_(capacity)
        , size_(0) {
    }

    template <typename... Args>
    static auto Make(Args&&... args) {
        return std::make_shared<MemoryCache>(std::forward<Args>(args)...);
    }

    size_t Size() const noexcept {
        std::lock_guard lock(mutex_);

        return size_;
    }

protected:
    DataHeader GetMeta(const HashId& id) const override {
        std::lock_guard lock(mutex_);

        if (auto oi = objects_.find(id); oi != objects_.end()) {
            const Object& obj = oi->second->second;

            return DataHeader::Make(obj.Type(), obj.Size());
        } else {
            return DataHeader();
        }
    }

    bool Exists(const HashId& id) const override {
        std::lock_guard lock(mutex_);

        return objects_.contains(id);
    }

    Object Load(const HashId& id, const DataType expected) const override {
        std::lock_guard lock(mutex_);

        if (auto oi = objects_.find(id); oi != objects_.end()) {
            // Type mismatch.
            if (Datastore::IsUnexpected(oi->second->second.Type(), expected)) {
                return Object();
            }
            list_.splice(list_.end(), list_, oi->second);
            return oi->second->second;
        } else {
            return Object();
        }
    }

    void Put(const HashId& id, DataType type, std::string_view content) override {
        InsertObject(id, Object::Load(type, content));
    }

    void Put(const HashId& id, const Object& obj) override {
        InsertObject(id, obj);
    }

private:
    void InsertObject(const HashId& id, Object obj) {
        std::lock_guard lock(mutex_);

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

private:
    using UsageList = std::list<std::pair<HashId, Object>>;

    mutable Mutex mutex_;
    size_t capacity_;
    size_t size_;
    mutable UsageList list_;
    absl::flat_hash_map<HashId, UsageList::iterator> objects_;
};

} // namespace Vcs::Store
