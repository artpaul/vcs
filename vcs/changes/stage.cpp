#include "stage.h"

#include <vcs/object/object.h>
#include <vcs/object/serialize.h>

#include <util/split.h>

#include <map>
#include <memory>

namespace Vcs {

class StageArea::Directory {
public:
    enum class Action {
        None = 0,
        Add = 1,
        Remove = 2,
    };

    struct Entry {
        HashId id{};
        Action action{Action::None};
        DataType data{DataType::None};
        PathType type{PathType::File};
        uint64_t size{0};
        std::unique_ptr<Directory> directory;
    };

public:
    static std::unique_ptr<StageArea::Directory> MakeEmpty();

    static std::unique_ptr<StageArea::Directory> FromTree(const Tree& tree);

public:
    /// List all entries.
    void ForEach(const std::function<void(const std::string& name, const Entry&)>& cb, bool removed = false)
        const;

    /// Find entry by name.
    Entry* Find(const std::string_view name, bool removed = false);

    /// Insert directory entry with the given name.
    Directory* MakeDirectory(const std::string_view name);

    /// Remove entry.
    bool Remove(const std::string_view name);

    /// Insert or update entry.
    bool Upsert(const std::string_view name, const PathEntry& e);

private:
    std::map<std::string, Entry, std::less<>> entries_;
};

auto StageArea::Directory::MakeEmpty() -> std::unique_ptr<Directory> {
    return std::make_unique<Directory>();
}

auto StageArea::Directory::FromTree(const Tree& tree) -> std::unique_ptr<Directory> {
    auto dir = std::make_unique<Directory>();

    for (const auto& e : tree.Entries()) {
        dir->entries_.emplace(
            e.Name(), Entry{.id = e.Id(), .data = e.Data(), .type = e.Type(), .size = e.Size()}
        );
    }

    return dir;
}

void StageArea::Directory::ForEach(
    const std::function<void(const std::string& name, const Entry&)>& cb, bool removed
) const {
    for (const auto& item : entries_) {
        if (item.second.action != Action::Remove || removed) {
            cb(item.first, item.second);
        }
    }
}

auto StageArea::Directory::Find(const std::string_view name, bool removed) -> Entry* {
    if (auto ei = entries_.find(name); ei != entries_.end()) {
        if (ei->second.action != Action::Remove || removed) {
            return &ei->second;
        }
    }
    return nullptr;
}

auto StageArea::Directory::MakeDirectory(const std::string_view name) -> Directory* {
    auto ei = entries_.find(name);
    if (ei != entries_.end()) {
        // Cleanup previous state.
        ei->second.id = HashId();
        ei->second.data = DataType::None;
        ei->second.size = 0;
    } else {
        ei = entries_.emplace(name, Entry()).first;
    }

    ei->second.action = Action::Add;
    ei->second.type = PathType::Directory;
    ei->second.directory = MakeEmpty();

    return ei->second.directory.get();
}

bool StageArea::Directory::Remove(const std::string_view name) {
    if (auto ei = entries_.find(name); ei != entries_.end()) {
        // If the entry was already scheduled for remove.
        if (ei->second.action == Action::Remove) {
            return false;
        }
        if (ei->second.id) {
            ei->second.action = Action::Remove;
            ei->second.directory.reset();
        } else {
            entries_.erase(ei);
        }
        return true;
    }
    return false;
}

bool StageArea::Directory::Upsert(const std::string_view name, const PathEntry& e) {
    auto ei = entries_.find(name);
    // Insert new entry if needed.
    if (ei == entries_.end()) {
        ei = entries_.emplace(name, Entry()).first;
    }
    // Setup fields.
    ei->second.action = Action::Add;
    ei->second.id = e.id;
    ei->second.data = e.data;
    ei->second.type = e.type;
    ei->second.size = e.size;
    ei->second.directory.reset();

    return true;
}

StageArea::StageArea(Datastore odb, const HashId& tree_id) noexcept
    : odb_(std::move(odb))
    , tree_id_(tree_id) {
    // Check that tree_id points to Tree object.
    assert(!tree_id_ || odb_.GetType(tree_id_, true) == DataType::Tree);
}

StageArea::~StageArea() = default;

bool StageArea::Add(const std::string_view path, const PathEntry& entry) {
    return AddImpl(path, entry, MutableRoot());
}

bool StageArea::Copy(const std::string& src, const std::string& dst) {
    if (const auto& entry = GetPathEntry(tree_id_, SplitPath(src))) {
        if (AddImpl(dst, *entry, MutableRoot())) {
            copies_[dst] = CommitPath{.id = HashId(), .path = std::string(src)};
            return true;
        }
    }
    return false;
}

std::optional<PathEntry> StageArea::GetEntry(const std::string_view path, bool removed) const {
    const auto& parts = SplitPath(path);

    if (parts.empty() || !stage_root_) {
        if (tree_id_) {
            return GetPathEntry(tree_id_, parts);
        } else {
            return PathEntry{.type = PathType::Directory};
        }
    }

    Directory* cur = stage_root_.get();

    for (size_t i = 0, end = parts.size(); i < end; ++i) {
        if (const auto e = cur->Find(parts[i], removed)) {
            if (i + 1 == parts.size()) {
                return PathEntry{.id = e->id, .data = e->data, .type = e->type, .size = e->size};
            } else if (e->directory) {
                // Just take in-memory directory.
                cur = e->directory.get();
            } else if (e->type == PathType::Directory) {
                // Try to find entry in the base tree.
                return GetPathEntry(e->id, {parts.begin() + i + 1, parts.end()});
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return std::nullopt;
}

std::vector<std::pair<std::string, PathEntry>> StageArea::ListTree(
    const std::string_view path, bool removed
) const {
    auto list_directory_entries = [&](const Directory* d) {
        std::vector<std::pair<std::string, PathEntry>> entries;

        d->ForEach(
            [&](const std::string& name, const Directory::Entry& e) {
                entries.emplace_back(
                    name, PathEntry{.id = e.id, .data = e.data, .type = e.type, .size = e.size}
                );
            },
            removed
        );

        return entries;
    };

    auto list_tree_entries = [&](const HashId tree_id, const std::vector<std::string_view>& parts) {
        std::vector<std::pair<std::string, PathEntry>> entries;

        if (const auto& ei = GetPathEntry(tree_id, parts)) {
            if (ei->type == PathType::Directory) {
                const auto tree = odb_.LoadTree(ei->id);

                entries.reserve(tree.Entries().size());

                for (const auto e : tree.Entries()) {
                    entries.emplace_back(e.Name(), static_cast<PathEntry>(e));
                }
            }
        }

        return entries;
    };

    const auto& parts = SplitPath(path);
    // List mutable root directory.
    if (parts.empty() && stage_root_) {
        return list_directory_entries(stage_root_.get());
    }
    // List entries from the base tree.
    if (!stage_root_) {
        return list_tree_entries(tree_id_, parts);
    }

    Directory* cur = stage_root_.get();

    for (size_t i = 0, end = parts.size(); i < end; ++i) {
        if (const auto e = cur->Find(parts[i], removed)) {
            if (i + 1 == end) {
                if (e->directory) {
                    return list_directory_entries(e->directory.get());
                } else if (IsDirectory(e->type)) {
                    return list_tree_entries(e->id, {});
                }
            } else if (e->directory) {
                cur = e->directory.get();
            } else if (IsDirectory(e->type)) {
                return list_tree_entries(e->id, {parts.begin() + i + 1, parts.end()});
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return std::vector<std::pair<std::string, PathEntry>>();
}

bool StageArea::Remove(const std::string_view path) {
    const auto& parts = SplitPath(path);
    Directory* cur = MutableRoot();

    for (size_t i = 0, end = parts.size(); i < end; ++i) {
        if (i + 1 == end) {
            if (cur->Remove(parts[i])) {
                copies_.erase(std::string(path));
                return true;
            }
            return false;
        }
        if (const auto e = cur->Find(parts[i])) {
            if (e->directory) {
                cur = e->directory.get();
            } else if (IsDirectory(e->type)) {
                e->directory = Directory::FromTree(odb_.LoadTree(e->id));
                cur = e->directory.get();
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return false;
}

HashId StageArea::SaveTree(Datastore odb, bool save_empty_directories) const {
    HashId id = tree_id_;
    // Save in-memory tree.
    if (stage_root_) {
        id = SaveTreeImpl(stage_root_.get(), std::move(odb), save_empty_directories).first;
    }
    // Ensure return value is always points to existed tree object.
    if (id) {
        return id;
    } else {
        return odb.Put(DataType::Tree, TreeBuilder().Serialize()).first;
    }
}

const std::map<std::string, CommitPath>& StageArea::CopyInfo() const {
    return copies_;
}

bool StageArea::AddImpl(const std::string_view path, const PathEntry& entry, Directory* root) {
    const auto& parts = SplitPath(path);

    for (size_t i = 0, end = parts.size(); i < end; ++i) {
        if (const auto e = root->Find(parts[i])) {
            if (i + 1 == end) {
                return root->Upsert(parts[i], entry);
            } else if (e->directory) {
                // Just take in-memory directory.
                root = e->directory.get();
            } else if (e->type == PathType::Directory) {
                // It's a directory node. Load it.
                if (e->id) {
                    e->directory = Directory::FromTree(odb_.LoadTree(e->id));
                } else {
                    e->directory = Directory::MakeEmpty();
                }
                root = e->directory.get();
            } else {
                // Reset type of the entry.
                root = root->MakeDirectory(parts[i]);
            }
        } else if (i + 1 == end) {
            return root->Upsert(parts[i], entry);
        } else {
            root = root->MakeDirectory(parts[i]);
        }
    }

    return false;
}

std::optional<PathEntry> StageArea::GetPathEntry(
    const HashId& id, const std::vector<std::string_view>& parts
) const {
    if (!bool(id)) {
        return std::nullopt;
    } else if (parts.empty()) {
        return PathEntry{.id = id, .data = DataType::Tree, .type = PathType::Directory};
    }

    Tree tree = odb_.LoadTree(id);

    for (size_t i = 0; i < parts.size(); ++i) {
        if (const auto e = tree.Find(parts[i])) {
            if (i + 1 == parts.size()) {
                return PathEntry{.id = e.Id(), .data = e.Data(), .type = e.Type(), .size = e.Size()};
            } else if (IsDirectory(e.Type())) {
                tree = odb_.LoadTree(e.Id());
            } else {
                break;
            }
        } else {
            break;
        }
    }

    return std::nullopt;
}

StageArea::Directory* StageArea::MutableRoot() {
    if (stage_root_) {
        return stage_root_.get();
    }
    if (tree_id_) {
        return (stage_root_ = Directory::FromTree(odb_.LoadTree(tree_id_))).get();
    } else {
        return (stage_root_ = Directory::MakeEmpty()).get();
    }
}

std::pair<HashId, DataType> StageArea::SaveTreeImpl(
    const Directory* root, Datastore odb, bool save_empty_directories
) const {
    TreeBuilder builder;

    root->ForEach([&](const std::string& name, const Directory::Entry& e) {
        PathEntry entry;

        if (e.directory) {
            entry.type = PathType::Directory;
            // Storage key.
            std::tie(entry.id, entry.data) = SaveTreeImpl(e.directory.get(), odb, save_empty_directories);

            if (!save_empty_directories && !entry.id) {
                return;
            }
        } else if (IsDirectory(e.type) && !e.id) {
            assert(e.action == Directory::Action::Add);
            assert(e.data == DataType::None);

            if (!save_empty_directories) {
                return;
            }

            entry.type = PathType::Directory;
            // Storage key.
            std::tie(entry.id, entry.data) = odb.Put(DataType::Tree, TreeBuilder().Serialize());
        } else {
            assert(e.id);
            assert(e.data != DataType::None);

            entry.id = e.id;
            entry.data = e.data;
            entry.type = e.type;
            entry.size = e.size;
        }

        builder.Append(name, std::move(entry));
    });

    if (builder.Empty() && !save_empty_directories) {
        return std::make_pair(HashId(), DataType::None);
    }

    return odb.Put(DataType::Tree, builder.Serialize());
}

HashId GetTreeId(const HashId& id, const Datastore& odb) {
    if (odb.GetType(id, true) == DataType::Tree) {
        return id;
    } else {
        return odb.LoadCommit(id).Tree();
    }
}

} // namespace Vcs
