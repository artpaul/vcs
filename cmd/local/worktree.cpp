#include "worktree.h"

#include <vcs/changes/changelist.h>
#include <vcs/common/ignore.h>
#include <vcs/object/serialize.h>
#include <vcs/store/memory.h>

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

    StatusState(std::string path, PathStatus::Status status)
        : path_(std::move(path))
        , status_(status) {
    }

    StatusState(std::string path, std::vector<std::pair<std::string, PathEntry>> entries)
        : path_(path) {
        for (auto& e : entries) {
            // Entries are sorted in general so it can be inserted with hint.
            entries_.emplace_hint(entries_.end(), std::move(e.first), std::make_pair(e.second, false));
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

    std::optional<PathStatus::Status> Status() const noexcept {
        return status_;
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
    std::optional<PathStatus::Status> status_;
};

} // namespace

WorkingTree::WorkingTree(const std::filesystem::path& path, Datastore odb, std::function<HashId()> cb)
    : path_(path)
    , odb_(std::move(odb))
    , get_tree_(std::move(cb)) {
    assert(path_.is_absolute());
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
            auto [id, type] = odb.Put(DataHeader::Make(DataType::Blob, size), InputStream(f));

            return PathEntry{
                .id = id,
                .data = type,
                .type = PathType::File,
                .size = size,
            };
        }
        case std::filesystem::file_type::symlink: {
            const std::string link = std::filesystem::read_symlink(file_path);
            const auto [id, type] = odb.Put(DataType::Blob, link);

            return PathEntry{
                .id = id,
                .data = type,
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
        // Ensure directory exists.
        CreateDirectory(p);

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
    Datastore odb = odb_.Cache(Store::MemoryCache::Make());
    StageArea stage(odb, tree_id);

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
    ChangelistBuilder(odb, std::move(cb))
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
        // Write blob.
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
        // Set executible bit.
        if (entry.type == PathType::Executible) {
            static constexpr std::filesystem::perms permissions =
                std::filesystem::perms::owner_all | std::filesystem::perms::group_read
                | std::filesystem::perms::group_exec | std::filesystem::perms::others_read
                | std::filesystem::perms::others_exec;

            std::filesystem::permissions(path, permissions);
        }
    }
}

static HashId CalculateFileHash(const std::filesystem::path& path) {
    auto file = File::ForRead(path);
    auto size = file.Size();
    HashId::Builder builder;
    char buf[8 << 10];
    builder.Append(DataHeader::Make(DataType::Blob, size));
    while (size > 0) {
        if (const size_t read = file.Read(buf, std::min(size, sizeof(buf)))) {
            builder.Append(buf, read);
            size -= read;
        } else {
            throw std::runtime_error(fmt::format("unexpected end of file '{}'", path));
        }
    }
    return builder.Build();
}

static Modifications CompareBlobEntry(
    const PathEntry& entry, const std::filesystem::directory_entry& de, const Datastore& odb
) {
    const auto status = de.symlink_status();
    Modifications result;

    if (status.type() == std::filesystem::file_type::regular) {
        result.type = entry.type == PathType::Symlink;
        result.attributes =
            (entry.type == PathType::Executible)
            ^ ((status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);

        if (entry.size != de.file_size()) {
            result.content = true;
        } else {
            const auto is_hash_mismatch = [&](const HashId& id) {
                if (entry.data == DataType::Index) {
                    return odb.LoadIndex(entry.id).Id() != id;
                } else {
                    return entry.id != id;
                }
            };

            result.content = is_hash_mismatch(CalculateFileHash(de.path()));
        }
    } else if (status.type() == std::filesystem::file_type::symlink) {
        const std::string link = std::filesystem::read_symlink(de.path());

        result.type = entry.type == PathType::File || entry.type == PathType::Executible;
        result.content = link.size() != entry.size || HashId::Make(DataType::Blob, link) != entry.id;
    }

    return result;
}

void WorkingTree::Status(const StatusOptions& options, const StageArea& stage, const StatusCallback& cb)
    const {
    auto di = std::filesystem::recursive_directory_iterator(path_);

    std::vector<std::tuple<IgnoreRules, std::filesystem::path, size_t>> ignores;
    std::vector<StatusState> state;

    const auto is_ignored = [&](const std::filesystem::path& path, const bool is_directory) {
        for (auto ri = ignores.rbegin(); ri != ignores.rend(); ++ri) {
            const auto value =
                std::get<0>(*ri).Match(path.lexically_relative(std::get<1>(*ri)).string(), is_directory);

            if (value) {
                return *value;
            }
        }

        return false;
    };

    const auto is_not_match = [&](const auto& path) {
        return !options.include.Match(path);
    };

    const auto is_not_parent = [&](const auto& path) {
        return !options.include.IsParent(path);
    };

    const auto emit_deleted = [&](const size_t depth) {
        while (state.size() > depth) {
            if (options.tracked) {
                state.back().EnumerateDeleted([&](const std::string& path, const PathEntry& entry) {
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

            state.pop_back();
        }

        while (ignores.size()) {
            if (std::get<2>(ignores.back()) >= depth) {
                ignores.pop_back();
            } else {
                break;
            }
        }
    };

    const auto try_load_ignore = [&](const std::filesystem::path& base, const size_t depth) {
        IgnoreRules rules;

        if (options.untracked == Expansion::None) {
            return;
        }

        if (rules.Load(base / ".gitignore")) {
            ignores.emplace_back(std::move(rules), base, depth);
        }
    };

    state.emplace_back(std::string(), stage.ListTree(std::string()));
    try_load_ignore(path_, 0);

    for (const auto& entry : di) {
        const auto filename = entry.path().filename().string();
        const auto path = entry.path().lexically_relative(path_).string();

        // Flush leaved directories.
        emit_deleted(di.depth() + 1);

        // Skip system entry.
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

            if (const auto ei = state.back().Find(filename)) {
                if (IsDirectory(ei->type)) {
                    // Emplace entries on entering a directory.
                    state.emplace_back(path, stage.ListTree(path));
                } else {
                    // type change
                }
            } else if (options.untracked == Expansion::None) {
                di.disable_recursion_pending();
            } else {
                const bool ignored =
                    state.back().Status() == PathStatus::Ignored || is_ignored(entry.path(), true);
                // Emit untracked status.
                if (ignored) {
                    if (options.ignored) {
                        cb(PathStatus()
                               .SetPath(path)
                               .SetStatus(PathStatus::Ignored)
                               .SetType(PathType::Directory));
                    }
                } else {
                    cb(PathStatus()
                           .SetPath(path)
                           .SetStatus(state.back().Status().value_or(PathStatus::Untracked))
                           .SetType(PathType::Directory));
                }
                // Skip subtree if there is no need to expand it.
                if (options.untracked != Expansion::All) {
                    di.disable_recursion_pending();
                } else if (ignored) {
                    if (options.ignored) {
                        state.emplace_back(path, PathStatus::Ignored);
                    } else {
                        di.disable_recursion_pending();
                    }
                } else {
                    state.emplace_back(path, PathStatus::Untracked);
                }
            }

            // Loading ignore rules even for untracked directories.
            if (di.recursion_pending()) {
                try_load_ignore(entry.path(), di.depth() + 1);
            }
        } else if (entry.is_regular_file() || entry.is_symlink()) {
            const auto path_type = entry.is_regular_file() ? PathType::File : PathType::Symlink;

            const auto emit_untracked = [&](std::string path, std::optional<PathEntry> pe) {
                assert(options.untracked != Expansion::None);

                if (state.back().Status() == PathStatus::Ignored || is_ignored(entry.path(), false)) {
                    if (options.ignored) {
                        cb(PathStatus()
                               .SetEntry(pe)
                               .SetPath(std::move(path))
                               .SetStatus(PathStatus::Ignored)
                               .SetType(path_type));
                    }
                } else {
                    cb(PathStatus()
                           .SetEntry(pe)
                           .SetPath(std::move(path))
                           .SetStatus(PathStatus::Untracked)
                           .SetType(path_type));
                }
            };

            if (is_not_match(path)) {
                continue;
            }

            if (const auto ei = state.back().Find(filename)) {
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
                        emit_untracked(path, *ei);
                    }
                } else if (options.tracked) {
                    if (const Modifications changes = CompareBlobEntry(*ei, entry, odb_)) {
                        cb(PathStatus()
                               .SetEntry(*ei)
                               .SetPath(path)
                               .SetStatus(PathStatus::Modified)
                               .SetType(path_type));
                    }
                }
            } else if (options.untracked != Expansion::None) {
                emit_untracked(path, {});
            }
        }
    }

    emit_deleted(0);
}

} // namespace Vcs
