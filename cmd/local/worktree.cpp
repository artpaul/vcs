#include "worktree.h"
#include "index.h"

#include <vcs/changes/changelist.h>
#include <vcs/common/ignore.h>
#include <vcs/object/serialize.h>
#include <vcs/store/memory.h>

#include <util/file.h>
#include <util/iterator.h>

#include <contrib/fmt/fmt/std.h>

#include <map>
#include <queue>
#include <stack>

namespace Vcs {
namespace {

class StatusState {
public:
    StatusState() = default;

    StatusState(std::string_view path, PathStatus::Status status)
        : path_(path)
        , status_(status) {
    }

    StatusState(std::string_view path, std::vector<std::pair<std::string, PathEntry>> entries)
        : path_(path)
        , entries_(std::move(entries)) {
        marks_.resize(entries_.size());
    }

    void EnumerateDeleted(const std::function<void(const std::string&, const PathEntry&)>& cb) const {
        for (size_t i = 0, end = entries_.size(); i != end; ++i) {
            if (marks_[i]) {
                continue;
            }

            cb(JoinPath(path_, entries_[i].first), entries_[i].second);
        }
    }

    const PathEntry* Find(const std::string_view name) {
        const auto ei = std::lower_bound(
            entries_.begin(), entries_.end(), name,
            [](const auto& item, const auto value) { return item.first < value; }
        );

        if (ei != entries_.end() && ei->first == name) {
            marks_[ei - entries_.begin()] = true;
            return &ei->second;
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
    std::vector<std::pair<std::string, PathEntry>> entries_;
    std::vector<bool> marks_;
    std::optional<PathStatus::Status> status_;
};

HashId CalculateFileHash(const std::filesystem::path& path) {
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

Modifications CompareBlobEntry(
    const std::filesystem::path& root,
    const std::string_view path,
    const PathEntry& entry,
    const DirectoryEntry* de,
    const Datastore& odb,
    TreeIndex* index
) {
    Modifications result;

    if (de->is_regular_file()) {
        result.type = entry.type == PathType::Symlink;
        result.attributes = (entry.type == PathType::Executible) ^ ((de->status()->st_mode & S_IEXEC) != 0);

        if (entry.size != de->status()->st_size) {
            result.content = true;
        } else {
            const auto get_id = [&]() {
                if (entry.data == DataType::Index) {
                    return odb.LoadIndex(entry.id).Id();
                } else {
                    return entry.id;
                }
            };

            const auto make_value = [&](const HashId& id) {
                return fmt::format(
                    "{}:{}:{}", int(de->status()->st_mode), id.ToBytes(), de->status()->st_mtim.tv_nsec
                );
            };

            if (auto ret = index->Get(path)) {
                if (*ret == make_value(get_id())) {
                    return result;
                }
            }

            const auto is_hash_mismatch = [&](const HashId& file_id) {
                index->Update(std::string(path), make_value(file_id));

                return get_id() != file_id;
            };

            result.content = is_hash_mismatch(CalculateFileHash(root / de->path()));
        }
    } else if (de->is_symlink()) {
        const std::string link = std::filesystem::read_symlink(root / de->path());

        result.type = entry.type == PathType::File || entry.type == PathType::Executible;
        result.content = link.size() != entry.size || HashId::Make(DataType::Blob, link) != entry.id;
    }

    return result;
}

} // namespace

WorkingTree::WorkingTree(
    const std::filesystem::path& path,
    const std::filesystem::path& state,
    Datastore odb,
    std::function<HashId()> cb
)
    : path_(path)
    , odb_(std::move(odb))
    , get_tree_(std::move(cb)) {
    assert(path_.is_absolute());
    assert(get_tree_);

    index_ = std::make_unique<TreeIndex>(state / "index", Lmdb::Options{.create_if_missing = true});
}

WorkingTree::~WorkingTree() = default;

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

void WorkingTree::Status(const StatusOptions& options, const StageArea& stage, const StatusCallback& cb)
    const {
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

    const auto is_not_match = [&](const std::string_view path) {
        return !options.include.Match(path);
    };

    const auto is_not_parent = [&](const std::string_view path) {
        return !options.include.IsParent(path);
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

    DirectoryIterator di(path_.string());

    index_->Start();
    state.emplace_back(std::string(), stage.ListTree(std::string()));
    try_load_ignore(path_, 0);

    while (const auto entry = di.Next()) {
        const auto filename = entry->filename();
        const auto path = entry->path();

        // Skip system entry.
        if (di.Depth() == 1 && filename == ".vcs") {
            di.DisableRecursionPending();
            continue;
        }

        if (entry->is_directory_enter()) {
            if (di.Depth() == 0) {
                continue;
            }
            // Skip subtree if it does not match the filter.
            if (is_not_parent(path)) {
                di.DisableRecursionPending();
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
                di.DisableRecursionPending();
            } else {
                const bool ignored =
                    state.back().Status() == PathStatus::Ignored || is_ignored(path_ / entry->path(), true);
                // Emit untracked status.
                if (ignored) {
                    if (options.ignored) {
                        cb(PathStatus()
                               .SetPath(std::string(path))
                               .SetStatus(PathStatus::Ignored)
                               .SetType(PathType::Directory));
                    }
                } else {
                    cb(PathStatus()
                           .SetPath(std::string(path))
                           .SetStatus(state.back().Status().value_or(PathStatus::Untracked))
                           .SetType(PathType::Directory));
                }
                // Skip subtree if there is no need to expand it.
                if (options.untracked != Expansion::All) {
                    di.DisableRecursionPending();
                } else if (ignored) {
                    if (options.ignored) {
                        state.emplace_back(path, PathStatus::Ignored);
                    } else {
                        di.DisableRecursionPending();
                    }
                } else {
                    state.emplace_back(path, PathStatus::Untracked);
                }
            }

            // Loading ignore rules even for untracked directories.
            if (di.RecursionPending()) {
                try_load_ignore(path_ / entry->path(), di.Depth());
            }
        } else if (entry->is_directory_exit()) {
            const auto depth = di.Depth();

            if (state.size() == depth + 1) {
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

            if (ignores.size() && std::get<2>(ignores.back()) == depth) {
                ignores.pop_back();
            }
        } else if (entry->is_regular_file() || entry->is_symlink()) {
            const auto path_type = entry->is_regular_file() ? PathType::File : PathType::Symlink;

            const auto emit_untracked = [&](std::optional<PathEntry> pe) {
                assert(options.untracked != Expansion::None);

                if (state.back().Status() == PathStatus::Ignored
                    || is_ignored(path_ / entry->path(), false)) {
                    if (options.ignored) {
                        cb(PathStatus()
                               .SetEntry(pe)
                               .SetPath(std::string(path))
                               .SetStatus(PathStatus::Ignored)
                               .SetType(path_type));
                    }
                } else {
                    cb(PathStatus()
                           .SetEntry(pe)
                           .SetPath(std::string(path))
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
                               .SetPath(std::string(path))
                               .SetStatus(PathStatus::Deleted)
                               .SetType(PathType::Directory));
                    }
                    // Current file entry.
                    if (options.untracked != Expansion::None) {
                        emit_untracked(*ei);
                    }
                } else if (options.tracked) {
                    di.Status();

                    const Modifications changes =
                        CompareBlobEntry(path_, path, *ei, entry, odb_, index_.get());

                    if (changes) {
                        cb(PathStatus()
                               .SetEntry(*ei)
                               .SetPath(std::string(path))
                               .SetStatus(PathStatus::Modified)
                               .SetType(path_type));
                    }
                }
            } else if (options.untracked != Expansion::None) {
                emit_untracked({});
            }
        }
    }

    assert(state.empty());
    assert(ignores.empty());

    index_->Flush();
}

} // namespace Vcs
