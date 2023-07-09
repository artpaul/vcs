#pragma once

#include "meta.h"
#include "mount.h"

#include <vcs/changes/stage.h>
#include <vcs/object/store.h>

#include <contrib/libfuse/include/fuse.h>

#include <optional>

namespace Vcs::Fs {

class Metabase;

struct BaseHandle {
    Meta meta;

    explicit BaseHandle(const Meta& m) noexcept
        : meta(m) {
    }
};

struct BlobHandle : BaseHandle {
    /// Flags which the file was opened with.
    const int flags = 0;
    /// Handle of an immutable blob object.
    std::optional<Object> blob;

    explicit BlobHandle(const Meta& m, Object obj) noexcept
        : BaseHandle(m)
        , blob(std::move(obj)) {
    }
};

struct DirectoryHandle : BaseHandle {
    Tree tree;

    explicit DirectoryHandle(const Meta& m, Tree obj) noexcept
        : BaseHandle(m)
        , tree(std::move(obj)) {
    }
};

/**
 * Virtual filesystem for working tree.
 */
class Filesystem {
public:
    explicit Filesystem(const MountOptions& options);

    ~Filesystem();

public:
    int Chmod(const std::string_view path, mode_t mode, fuse_file_info* fi);

    int GetAttr(const std::string_view path, struct stat*, fuse_file_info*);

    int Open(const std::string_view path, fuse_file_info* fi);

    int OpenDir(const std::string_view path, fuse_file_info* fi);

    int Read(char* buf, size_t size, off_t offset, fuse_file_info* fi);

    int ReadDir(void* buf, fuse_fill_dir_t filler, off_t off, fuse_file_info* fi, enum fuse_readdir_flags);

    int ReadLink(const std::string_view path, char* buf, size_t size);

    int Release(struct fuse_file_info* fi);

    int ReleaseDir(struct fuse_file_info* fi);

    int StatFs(struct statvfs* fs);

private:
    Meta GetActualMetadata(const std::string_view path, const PathEntry& e) const;

    void SetupAttributes(const PathEntry& e, struct stat* st) const noexcept;

private:
    Datastore odb_;
    Datastore blobs_;
    Datastore trees_;
    StageArea stage_;
    HashId tree_;
    timespec root_time_;

    std::unique_ptr<Metabase> metabase_;

    uid_t euid_;
    uid_t egid_;
};

} // namespace Vcs::Fs
