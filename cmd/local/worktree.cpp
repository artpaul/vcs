#include "worktree.h"

#include <util/file.h>

#include <contrib/fmt/fmt/std.h>

#include <queue>

namespace Vcs {

WorkingTree::WorkingTree(const std::filesystem::path& path, Datastore odb)
    : path_(path)
    , odb_(std::move(odb)) {
}

void WorkingTree::Checkout(const HashId& tree_id) {
    if (tree_id) {
        MakeTree(path_, odb_.LoadTree(tree_id));
    }
}

void WorkingTree::Checkout(const std::string& path, const PathEntry& entry) {
    (void)path;
    (void)entry;
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
            auto file = File::ForAppend(path);
            file.Write(obj.Data(), obj.Size());
        } else if (obj.Type() == DataType::Index) {
            auto file = File::ForAppend(path);
            for (const auto& part : obj.AsIndex().Parts()) {
                auto blob = odb_.LoadBlob(part.Id());
                file.Write(blob.Data(), blob.Size());
            }
        }
    }
}

void WorkingTree::Status(const StatusOptions& options, const StageArea& stage, const StatusCallback& cb)
    const {
    auto di = std::filesystem::recursive_directory_iterator(path_);

    (void)options;
    (void)stage;

    for (const auto& entry : di) {
        const auto& filename = entry.path().filename();
        const auto& path = std::filesystem::relative(entry.path(), path_);

        if (entry.is_directory()) {
            if (di.depth() == 0 && filename == ".vcs") {
                di.disable_recursion_pending();
                continue;
            }
        } else if (entry.is_regular_file()) {
            cb(PathStatus().SetPath(path).SetStatus(PathStatus::Untracked).SetType(PathType::File));
        }
    }
}

} // namespace Vcs
