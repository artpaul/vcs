#pragma once

#include <filesystem>

namespace Vcs {

class Layout {
public:
    explicit Layout(std::filesystem::path root)
        : root_(std::move(root)) {
    }

public:
    /**
     * Locations of configuration files.
     */
    std::filesystem::path Configs() const {
        return root_ / "config";
    }

    /**
     * Locations of local databases.
     */
    std::filesystem::path Databases() const {
        return root_ / "db";
    }

    std::filesystem::path Database(const std::string& name) const {
        return root_ / "db" / name;
    }

public:
    /**
     * Locations of loose object storage.
     */
    std::filesystem::path Objects() const {
        return root_ / "store" / "objects";
    }

    /**
     * Locations of pack storage.
     */
    std::filesystem::path Packs() const {
        return root_ / "store" / "packs";
    }

public:
    /**
     * Location of remotes.
     */
    std::filesystem::path Remotes() const {
        return root_ / "remotes";
    }

public:
    /**
     * Location of workspaces.
     */
    std::filesystem::path Workspaces() const {
        return root_ / "workspaces";
    }

    std::filesystem::path Workspace(const std::string& name) const {
        return root_ / "workspaces" / name;
    }

private:
    std::filesystem::path root_;
};

} // namespace Vcs
