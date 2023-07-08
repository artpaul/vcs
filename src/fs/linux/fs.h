#pragma once

#include "mount.h"

#include <vcs/changes/stage.h>
#include <vcs/object/store.h>

#include <contrib/libfuse/include/fuse.h>

#include <optional>

namespace Vcs::Fs {

struct BlobHandle {
    /// Flags which the file was opened with.
    const int flags = 0;
    /// Handle of an immutable blob object.
    std::optional<Object> blob;

    explicit BlobHandle(Object obj) noexcept
        : blob(std::move(obj)) {
    }
};

struct DirectoryHandle {
    Tree tree;

    explicit DirectoryHandle(Tree obj) noexcept
        : tree(std::move(obj)) {
    }
};

/**
 * Virtual filesystem for working tree.
 */
class Filesystem {
public:
    explicit Filesystem(const MountOptions& options);

public:
    int GetAttr(const std::string_view path, struct stat*, struct fuse_file_info*);

    int Open(const std::string_view path, struct fuse_file_info* fi);

    int OpenDir(const std::string_view path, struct fuse_file_info* fi);

    int Read(char* buf, size_t size, off_t offset, struct fuse_file_info* fi);

    int ReadDir(
        void* buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info* fi, enum fuse_readdir_flags
    );

    int ReadLink(const std::string_view path, char* buf, size_t size);

    int Release(struct fuse_file_info* fi);

    int ReleaseDir(struct fuse_file_info* fi);

    int StatFs(struct statvfs* fs);

private:
    void SetupAttributes(const PathEntry& e, struct stat* st) const noexcept;

private:
    Datastore odb_;
    Datastore blobs_;
    Datastore trees_;
    StageArea stage_;
    HashId tree_;
    time_t start_time_;

    uid_t euid_;
    uid_t egid_;
};

} // namespace Vcs::Fs
