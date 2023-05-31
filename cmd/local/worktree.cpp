#include "worktree.h"

#include <vcs/changes/changelist.h>
#include <vcs/object/serialize.h>

#include <util/file.h>

#include <contrib/fmt/fmt/std.h>

#include <map>
#include <queue>
#include <stack>

namespace Vcs {
namespace {

class StatusState {
public:
    StatusState() = default;

    StatusState(std::string path)
        : path_(std::move(path)) {
    }

    StatusState(std::string path, const std::vector<std::pair<std::string, PathEntry>>& entries)
        : path_(path) {
        for (const auto& e : entries) {
            entries_.emplace(e.first, std::make_pair(e.second, false));
        }
    }

    void EnumerateDeleted(const std::function<void(const std::string&, const PathEntry&)>& cb) const {
        for (const auto& e : entries_) {
            if (e.second.second) {
                continue;
            }

            cb(JoinPath(path_, e.first), e.second.first);
        }
    }

    const PathEntry* Find(const std::string_view name) {
        if (auto ei = entries_.find(name); ei != entries_.end()) {
            ei->second.second = true;
            return &ei->second.first;
        }
        return nullptr;
    }

    std::string JoinPath(const std::string_view name) const {
        return JoinPath(path_, name);
    }

private:
    static std::string JoinPath(std::string path, const std::string_view name) {
        if (path.empty()) {
            return std::string(name);
        } else {
            path.append("/");
            path.append(name);
            return path;
        }
    }

private:
    std::string path_;
    std::map<std::string, std::pair<PathEntry, bool>, std::less<>> entries_;
};

} // namespace

WorkingTree::WorkingTree(const std::filesystem::path& path, Datastore odb, std::function<HashId()> cb)
    : path_(path)
    , odb_(std::move(odb))
    , get_tree_(std::move(cb)) {
    assert(get_tree_);
}

const std::filesystem::path& WorkingTree::GetPath() const {
    return path_;
}

std::optional<PathEntry> WorkingTree::MakeBlob(const std::string& path, Datastore odb) const {
    const auto file_path = path_ / path;

    switch (std::filesystem::symlink_status(file_path).type()) {
        case std::filesystem::file_type::regular: {
            auto f = File::ForRead(file_path, false);
            auto size = f.Size();

            return PathEntry{
                .id = odb.Put(DataHeader::Make(DataType::Blob, size), InputStream(f)),
                .type = PathType::File,
                .size = size,
            };
        }
        case std::filesystem::file_type::symlink: {
            const std::string link = std::filesystem::read_symlink(file_path);

            return PathEntry{
                .id = odb.Put(DataType::Blob, link),
                .type = PathType::Symlink,
                .size = link.size(),
            };
        }
        default:
            break;
    }

    return std::nullopt;
}

void WorkingTree::CreateDirectory(const std::string& p) {
    const auto path = path_ / p;
    const auto status = std::filesystem::symlink_status(path);

    if (status.type() == std::filesystem::file_type::not_found) {
        // Create directory.
        std::filesystem::create_directories(path);
    } else if (status.type() == std::filesystem::file_type::directory) {
        // Already a directory.
    } else {
        // Remove the current state.
        std::filesystem::remove_all(path);
        // Create directory.
        std::filesystem::create_directories(path);
    }
}

void WorkingTree::Checkout(const HashId& tree_id) {
    if (tree_id) {
        MakeTree(path_, odb_.LoadTree(tree_id));
    }
}

void WorkingTree::Checkout(const std::string& p, const PathEntry& entry) {
    const auto path = path_ / p;

    if (IsDirectory(entry.type)) {
        MakeTree(path, odb_.LoadTree(entry.id));
    } else {
        const auto status = std::filesystem::symlink_status(path);

        // Remove the current state if required.
        if (status.type() == std::filesystem::file_type::directory) {
            std::filesystem::remove_all(path);
        }

        WriteBlob(path, entry);
    }
}

bool WorkingTree::SwitchTo(const HashId& tree_id) {
    StageArea stage(odb_, tree_id); // TODO: mem cache.

    auto cb = [&](const Change& change) {
        if (change.action == PathAction::Add || change.action == PathAction::Change) {
            if (change.type == PathType::Directory) {
                CreateDirectory(change.path);
            } else if (const auto& e = stage.GetEntry(change.path)) {
                WriteBlob(path_ / change.path, *e);
            } else {
                // Reported entry should always be in the target tree.
                assert(false);
            }
        } else if (change.action == PathAction::Delete) {
            std::filesystem::remove_all(path_ / change.path);
        }
    };

    // Caclculate changelist.
    ChangelistBuilder(odb_, std::move(cb))
        .SetExpandAdded(true)
        .SetExpandDeleted(false)
        .Changes(get_tree_(), tree_id);

    return true;
}

void WorkingTree::MakeTree(const std::filesystem::path& root, const Tree tree) const {
    std::queue<std::tuple<std::filesystem::path, Tree, Tree::Entry>> queue;

    const auto enqueue_entrie = [&queue](const std::filesystem::path& root, const Tree& tree) {
        for (const auto& e : tree.Entries()) {
            queue.emplace(root / e.Name(), tree, e);
        }
    };

    enqueue_entrie(root, tree);

    for (; !queue.empty(); queue.pop()) {
        const auto& [path, tree, e] = queue.front();
        const auto status = std::filesystem::symlink_status(path);

        // Remove the current state if required.
        if (status.type() != std::filesystem::file_type::not_found) {
            if (!IsDirectory(e.Type()) || status.type() != std::filesystem::file_type::directory) {
                std::filesystem::remove_all(path);
            }
        }

        if (IsDirectory(e.Type())) {
            // Create a directory.
            if (status.type() != std::filesystem::file_type::directory) {
                std::filesystem::create_directory(path);
            }
            // Enqueue a subtree.
            enqueue_entrie(path, odb_.LoadTree(e.Id()));
        } else {
            // Write a file.
            WriteBlob(path, PathEntry{.id = e.Id(), .type = e.Type(), .size = e.Size()});
        }
    }
}

void WorkingTree::WriteBlob(const std::filesystem::path& path, const PathEntry& entry) const {
    if (entry.type == PathType::Symlink) {
        std::filesystem::create_symlink(std::string_view(odb_.LoadBlob(entry.id)), path);
        return;
    }

    if (entry.type == PathType::Executible || entry.type == PathType::File) {
        auto obj = odb_.Load(entry.id);

        if (obj.Type() == DataType::Blob) {
            auto file = File::ForOverwrite(path);
            file.Write(obj.Data(), obj.Size());
        } else if (obj.Type() == DataType::Index) {
            auto file = File::ForOverwrite(path);
            for (const auto& part : obj.AsIndex().Parts()) {
                auto blob = odb_.LoadBlob(part.Id());
                file.Write(blob.Data(), blob.Size());
            }
        }
    }
}

static bool
CompareBlobEntry(const PathEntry& entry, const std::filesystem::directory_entry& de, const Datastore&) {
    if (de.is_regular_file()) {
        return entry.size == de.file_size();
    } else if (de.is_symlink()) {
        ;
    }

    return true;
}

void WorkingTree::Status(const StatusOptions& options, const StageArea& stage, const StatusCallback& cb)
    const {
    auto di = std::filesystem::recursive_directory_iterator(path_);

    std::stack<StatusState> state;

    state.emplace(std::string(), stage.ListTree(std::string()));

    const auto is_not_match = [&](const auto& path) {
        return !options.include.Match(path);
    };

    const auto is_not_parent = [&](const auto& path) {
        return !options.include.IsParent(path);
    };

    const auto emit_deleted = [&](const size_t depth) {
        while (state.size() > depth) {
            if (options.tracked) {
                state.top().EnumerateDeleted([&](const std::string& path, const PathEntry& entry) {
                    if (is_not_match(path)) {
                        return;
                    }
                    cb(PathStatus()
                           .SetEntry(entry)
                           .SetPath(path)
                           .SetStatus(PathStatus::Deleted)
                           .SetType(entry.type));
                });
            }

            state.pop();
        }
    };

    for (const auto& entry : di) {
        const auto& filename = entry.path().filename().string();
        const auto& path = std::filesystem::relative(entry.path(), path_).string();

        //
        emit_deleted(di.depth() + 1);

        //
        if (di.depth() == 0 && filename == ".vcs") {
            if (entry.is_directory()) {
                di.disable_recursion_pending();
            }
            continue;
        }

        if (entry.is_directory()) {
            // Skip subtree if it does not match the filter.
            if (is_not_parent(path)) {
                di.disable_recursion_pending();
                continue;
            }

            if (const auto ei = state.top().Find(filename)) {
                if (IsDirectory(ei->type)) {
                    // Emplace entries on entering a directory.
                    state.emplace(path, stage.ListTree(path));
                } else {
                    // type change
                }
            } else {
                // Emit untracked status.
                if (options.untracked != Expansion::None) {
                    cb(PathStatus()
                           .SetPath(path)
                           .SetStatus(PathStatus::Untracked)
                           .SetType(PathType::Directory));
                }
                // Skip subtree if there is no need to expand it.
                if (options.untracked != Expansion::All) {
                    di.disable_recursion_pending();
                }
            }
        } else if (entry.is_regular_file() || entry.is_symlink()) {
            const auto path_type = entry.is_regular_file() ? PathType::File : PathType::Symlink;

            if (is_not_match(path)) {
                continue;
            }

            if (const auto ei = state.top().Find(filename)) {
                if (IsDirectory(ei->type)) {
                    // Previous directory entry.
                    if (options.tracked) {
                        cb(PathStatus()
                               .SetEntry(*ei)
                               .SetPath(path)
                               .SetStatus(PathStatus::Deleted)
                               .SetType(PathType::Directory));
                    }
                    // Current file entry.
                    if (options.untracked != Expansion::None) {
                        cb(PathStatus()
                               .SetEntry(*ei)
                               .SetPath(path)
                               .SetStatus(PathStatus::Untracked)
                               .SetType(path_type));
                    }
                } else if (options.tracked) {
                    if (CompareBlobEntry(*ei, entry, odb_)) {
                        continue;
                    }

                    cb(PathStatus()
                           .SetEntry(*ei)
                           .SetPath(path)
                           .SetStatus(PathStatus::Modified)
                           .SetType(path_type));
                }
            } else if (options.untracked != Expansion::None) {
                cb(PathStatus().SetPath(path).SetStatus(PathStatus::Untracked).SetType(path_type));
            }
        }
    }

    emit_deleted(0);
}

} // namespace Vcs
