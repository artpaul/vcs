#pragma once

#include "meta.h"
#include "mount.h"

#include <vcs/changes/stage.h>
#include <vcs/object/store.h>

#include <contrib/libfuse/include/fuse.h>

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <variant>

namespace Vcs::Fs {

class Metabase;

/**
 * Virtual filesystem for working tree.
 */
class Filesystem {
    struct Directory {
        struct Entry {
            Meta meta;
            std::shared_ptr<Directory> directory;
        };

        std::mutex mutex;
        std::map<std::string, Entry, std::less<>> entries;

        static std::shared_ptr<Directory> MakeEmpty();

        static std::shared_ptr<Directory> MakeFromTree(const Tree& tree, const Timestamps& ts);
    };

    struct BaseHandle {
        Meta meta;

        explicit BaseHandle(const Meta& m) noexcept
            : meta(m) {
        }
    };

    struct DirectoryHandle : BaseHandle {
        std::variant<Tree, std::map<std::string, Directory::Entry, std::less<>>> entries;

        DirectoryHandle(const Meta& m, Tree obj) noexcept
            : BaseHandle(m)
            , entries(std::move(obj)) {
        }

        DirectoryHandle(const Meta& m, std::map<std::string, Directory::Entry, std::less<>> obj)
            : BaseHandle(m)
            , entries(std::move(obj)) {
        }
    };

    struct FileHandle : BaseHandle {
        /// File descriptor.
        std::variant<Object, int> fd;

        FileHandle(const Meta& m, Object obj) noexcept
            : BaseHandle(m)
            , fd(std::move(obj)) {
        }

        FileHandle(const Meta& m, int f) noexcept
            : BaseHandle(m)
            , fd(f) {
        }
    };

    using DirectoryPtr = std::shared_ptr<Directory>;

public:
    explicit Filesystem(const MountOptions& options);

    ~Filesystem();

public:
    int Create(const std::string_view path, mode_t mode, fuse_file_info* fi);

    int Chmod(const std::string_view path, mode_t mode, fuse_file_info* fi);

    int GetAttr(const std::string_view path, struct stat*, fuse_file_info*);

    int Mkdir(const std::string_view path, mode_t mode);

    int Open(const std::string_view path, fuse_file_info* fi);

    int OpenDir(const std::string_view path, fuse_file_info* fi);

    int Read(char* buf, size_t size, off_t offset, fuse_file_info* fi);

    int ReadDir(void* buf, fuse_fill_dir_t filler, off_t off, fuse_file_info* fi, enum fuse_readdir_flags);

    int ReadLink(const std::string_view path, char* buf, size_t size);

    int Release(struct fuse_file_info* fi);

    int ReleaseDir(struct fuse_file_info* fi);

    int Rmdir(const std::string_view path);

    int StatFs(struct statvfs* fs);

    int Utimens(const std::string_view path, const timespec tv[2], fuse_file_info* fi);

private:
    Meta GetActualMetadata(const std::string_view path, const PathEntry& e) const;

    DirectoryPtr GetMutableParentNoLock(const std::vector<std::string_view>& parts, bool materialize);

    void LoadSate();

    void SetupAttributes(const PathEntry& e, struct stat* st) const noexcept;

private:
    Datastore odb_;
    Datastore blobs_;
    Datastore trees_;
    StageArea stage_;
    HashId tree_;
    std::filesystem::path mutable_path_;

    std::shared_mutex root_mutex_;
    DirectoryPtr root_;
    timespec root_time_;

    std::unique_ptr<Metabase> metabase_;

    uid_t euid_;
    uid_t egid_;
};

} // namespace Vcs::Fs