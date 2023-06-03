#include "converter.h"

#include <vcs/changes/stage.h>
#include <vcs/changes/validate.h>
#include <vcs/object/commit.h>
#include <vcs/object/serialize.h>
#include <vcs/object/store.h>
#include <vcs/store/memory.h>

#include <contrib/fmt/fmt/format.h>
#include <contrib/libgit2/include/git2.h>

namespace Vcs::Git {
namespace {

void CheckError(int error_code, const char* action) {
    if (const git_error* error = git_error_last()) {
        throw std::runtime_error(fmt::format(
            "{} {} - {}\n", error_code, action, (error && error->message) ? error->message : "???"
        ));
    }
}

class Builder {
public:
    Builder(Datastore& odb, StageArea& stage, git_repository* repo)
        : odb_(odb)
        , stage_(stage)
        , repo_(repo) {
    }

    Builder& UseBlobCache(bool value) {
        if (value) {
            blob_cache_ = std::make_unique<std::unordered_map<HashId, std::pair<HashId, uint32_t>>>();
        } else {
            blob_cache_.reset();
        }
        return *this;
    }

    void Apply(git_diff* diff) {
        git_diff_foreach(diff, &Builder::FileCb, nullptr, nullptr, nullptr, this);
    }

private:
    static int FileCb(const git_diff_delta* delta, float, void* payload) {
        static_cast<Builder*>(payload)->Delta(delta);
        return 0;
    }

    void Delta(const git_diff_delta* delta) {
        // Submodules are not supported right now.
        if (delta->new_file.mode == GIT_FILEMODE_COMMIT) {
            return;
        }

        switch (delta->status) {
            case GIT_DELTA_ADDED:
                if (!stage_.Add(delta->new_file.path, MakeBlob(delta->new_file))) {
                    throw std::runtime_error(fmt::format("cannot add path {}", delta->new_file.path));
                }
                break;
            case GIT_DELTA_DELETED:
                if (!stage_.Remove(delta->old_file.path)) {
                    throw std::runtime_error(fmt::format("cannot delete path {}", delta->old_file.path));
                }
                break;
            case GIT_DELTA_MODIFIED:
                if (!stage_.Add(delta->new_file.path, MakeBlob(delta->new_file))) {
                    throw std::runtime_error(fmt::format("cannot update path {}", delta->new_file.path));
                }
                break;
            case GIT_DELTA_RENAMED:
                // Setup copy-info.
                if (!stage_.Copy(delta->old_file.path, delta->new_file.path)) {
                    throw std::runtime_error(fmt::format("cannot copy path {}", delta->new_file.path));
                }
                // Update destination entry,
                if (!stage_.Add(delta->new_file.path, MakeBlob(delta->new_file))) {
                    throw std::runtime_error(fmt::format("cannot update path {}", delta->new_file.path));
                }
                // Remove source entry.
                if (!stage_.Remove(delta->old_file.path)) {
                    throw std::runtime_error(fmt::format("cannot delete path {}", delta->old_file.path));
                }
                break;
            case GIT_DELTA_COPIED:
                // Setup copy-info.
                if (!stage_.Copy(delta->old_file.path, delta->new_file.path)) {
                    throw std::runtime_error(fmt::format("cannot copy path {}", delta->new_file.path));
                }
                // Update destination entry,
                if (!stage_.Add(delta->new_file.path, MakeBlob(delta->new_file))) {
                    throw std::runtime_error(fmt::format("cannot update path {}", delta->new_file.path));
                }
                break;
            default:
                break;
        }
    }

    PathEntry MakeBlob(const git_diff_file& file) const {
        PathEntry entry;
        // Setup type.
        entry.type = PathTypeFromMode(file.mode);
        // Try to setup id and size from cache.
        if (blob_cache_) {
            if (auto bi = blob_cache_->find(HashId::FromBytes(file.id.id)); bi != blob_cache_->end()) {
                entry.id = bi->second.first;
                entry.size = bi->second.second;
                return entry;
            }
        }

        std::unique_ptr<git_blob, std::function<void(git_blob*)>> blob(
            [&]() {
                git_blob* r;
                CheckError(::git_blob_lookup(&r, repo_, &file.id), "blob lookup");
                return r;
            }(),
            [](git_blob* r) { ::git_blob_free(r); }
        );

        // Save blob content.
        entry.size = git_blob_rawsize(blob.get());
        // Storage key.
        std::tie(entry.id, entry.data) = odb_.Put(
            DataType::Blob,
            std::string_view(
                reinterpret_cast<const char*>(git_blob_rawcontent(blob.get())), git_blob_rawsize(blob.get())
            )
        );

        if (blob_cache_) {
            blob_cache_->emplace(HashId::FromBytes(file.id.id), std::make_pair(entry.id, entry.size));
        }

        return entry;
    }

    constexpr PathType PathTypeFromMode(const uint16_t mode) const noexcept {
        if (mode == GIT_FILEMODE_TREE) {
            return PathType::Directory;
        }
        if (mode == GIT_FILEMODE_BLOB) {
            return PathType::File;
        }
        if (mode == GIT_FILEMODE_BLOB_EXECUTABLE) {
            return PathType::Executible;
        }
        if (mode == GIT_FILEMODE_LINK) {
            return PathType::Symlink;
        }
        return PathType::Unknown;
    }

private:
    Datastore& odb_;
    StageArea& stage_;
    git_repository* const repo_;
    std::unique_ptr<std::unordered_map<HashId, std::pair<HashId, uint32_t>>> blob_cache_;
};

} // namespace

class Converter::Impl {
public:
    Impl(const std::filesystem::path& path, const Options& options);

    ~Impl();

    void SetRemap(std::function<HashId(const HashId&)> remap);

    HashId ConvertCommit(const HashId& id, Datastore odb);

    void ListCommits(const std::string& head, const std::function<WalkAction(const HashId&)>& cb) const;

private:
    Options options_;
    git_repository* repo_{nullptr};
    std::function<HashId(const HashId&)> remap_;
    std::shared_ptr<Store::MemoryCache> tree_cache_;
};

Converter::Impl::Impl(const std::filesystem::path& path, const Options& options)
    : options_(options) {
    ::git_libgit2_init();

    CheckError(::git_repository_open_bare(&repo_, path.c_str()), "opening repository");

    tree_cache_ = Store::MemoryCache::Make(32u << 20);
}

Converter::Impl::~Impl() {
    if (repo_) {
        git_repository_free(repo_);
    }

    ::git_libgit2_shutdown();
}

void Converter::Impl::SetRemap(std::function<HashId(const HashId&)> remap) {
    remap_ = std::move(remap);
}

HashId Converter::Impl::ConvertCommit(const HashId& id, Datastore odb) {
    git_oid oid;

    assert(remap_);
    static_assert(sizeof(oid.id) == sizeof(id.Data()));

    std::memcpy(oid.id, id.Data(), sizeof(oid.id));

    std::unique_ptr<git_commit, std::function<void(git_commit*)>> wcommit(
        [&]() {
            git_commit* r;
            CheckError(git_commit_lookup(&r, repo_, &oid), "looking up commit during revwalk");
            return r;
        }(),
        [](git_commit* r) { ::git_commit_free(r); }
    );

    std::unique_ptr<git_tree, std::function<void(git_tree*)>> tree(
        [&]() {
            git_tree* r;
            CheckError(git_commit_tree(&r, wcommit.get()), "looking up tree");
            return r;
        }(),
        [](git_tree* r) { ::git_tree_free(r); }
    );

    CommitBuilder builder;

    // Validate parent commits already converted.
    for (size_t i = 0, end = git_commit_parentcount(wcommit.get()); i < end; ++i) {
        const auto id = HashId::FromBytes(git_commit_parent_id(wcommit.get(), i)->id);

        if (const auto ret = remap_(id)) {
            builder.parents.push_back(ret);
        } else {
            throw std::runtime_error(fmt::format("cannot local converted commit '{}'", id));
        }
    }

    auto stage_odb = odb.Cache(tree_cache_);
    std::unique_ptr<StageArea> stage;

    {
        std::unique_ptr<git_diff, std::function<void(git_diff*)>> diff(nullptr, [](git_diff* r) {
            ::git_diff_free(r);
        });

        git_diff_options options{};
        git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
        options.flags = GIT_DIFF_NORMAL | GIT_DIFF_SKIP_BINARY_CHECK;

        if (git_commit_parentcount(wcommit.get())) {
            const git_oid* parent_oid = git_commit_parent_id(wcommit.get(), 0);

            std::unique_ptr<git_commit, std::function<void(git_commit*)>> parent_commit(
                [&]() {
                    git_commit* r;
                    CheckError(git_commit_lookup(&r, repo_, parent_oid), "parent commit");
                    return r;
                }(),
                [](git_commit* r) { ::git_commit_free(r); }
            );

            std::unique_ptr<git_tree, std::function<void(git_tree*)>> parent_tree(
                [&]() {
                    git_tree* r;
                    CheckError(git_commit_tree(&r, parent_commit.get()), "parent tree");
                    return r;
                }(),
                [](git_tree* r) { ::git_tree_free(r); }
            );

            {
                git_diff* r{};
                git_diff_tree_to_tree(&r, repo_, parent_tree.get(), tree.get(), &options);
                diff.reset(r);
            }

            // Detection of renames currently not supported for merge commits.
            if (options_.detect_renames && git_commit_parentcount(wcommit.get()) == 1) {
                git_diff_find_options find_opts{};

                find_opts.rename_threshold = 75;
                find_opts.rename_from_rewrite_threshold = 75;
                find_opts.copy_threshold = 75;
                find_opts.break_rewrite_threshold = 60;
                find_opts.rename_limit = 5000;
                find_opts.version = GIT_DIFF_FIND_OPTIONS_VERSION;

                CheckError(git_diff_find_similar(diff.get(), &find_opts), "detect renames");
            }

            stage = std::make_unique<StageArea>(stage_odb, GetTreeId(builder.parents[0], stage_odb));
        } else {
            {
                git_diff* r{};
                git_diff_tree_to_tree(&r, repo_, nullptr, tree.get(), &options);
                diff.reset(r);
            }
            stage = std::make_unique<StageArea>(stage_odb);
        }

        Builder(odb, *stage, repo_).UseBlobCache(options_.use_blob_chache).Apply(diff.get());
    }

    if (options_.store_original_hash) {
        builder.attributes.push_back(CommitBuilder::Attribute{.name = "git-hash", .value = id.ToHex()});
    }

    if (const auto author = git_commit_author(wcommit.get())) {
        builder.author.id = author->email;
        builder.author.name = author->name;
        builder.author.when = author->when.time; // TODO: offset
    }
    if (const auto commiter = git_commit_committer(wcommit.get())) {
        builder.committer.id = commiter->email;
        builder.committer.name = commiter->name;
        builder.committer.when = commiter->when.time; // TODO: offset
    }

    if (const auto& copies = stage->CopyInfo(); !copies.empty()) {
        RenamesBuilder renames;

        for (const auto& [path, source] : copies) {
            renames.copies.push_back(RenamesBuilder::CopyInfo{
                .commit = source.id ? source.id : builder.parents.at(0),
                .source = source.path,
                .path = path,
            });
        }

        builder.renames = odb.Put(DataType::Renames, renames.Serialize()).first;
    }

    //
    builder.message = git_commit_message(wcommit.get());
    builder.tree = stage->SaveTree(stage_odb, false);
    builder.generation = 1 + GetLargestGeneration(builder, stage_odb);

    tree.reset();
    wcommit.reset();

    const auto& content = builder.Serialize();
    // Check consistency.
    if (!CheckConsistency(Object::Load(DataType::Commit, content), stage_odb)) {
        throw std::runtime_error("inconsistent commit object");
    }
    // Save to databaes.
    return stage_odb.Put(DataType::Commit, content).first;
}

void Converter::Impl::ListCommits(
    const std::string& head, const std::function<WalkAction(const HashId&)>& cb
) const {
    std::unique_ptr<git_reference, std::function<void(git_reference*)>> ref(
        [&]() {
            git_reference* ref;
            CheckError(
                ::git_branch_lookup(&ref, repo_, head.c_str(), GIT_BRANCH_LOCAL), "reference lookup"
            );
            return ref;
        }(),
        [](git_reference* r) { ::git_reference_free(r); }
    );

    std::unique_ptr<git_revwalk, std::function<void(git_revwalk*)>> walk(
        [&]() {
            git_revwalk* r;
            CheckError(::git_revwalk_new(&r, repo_), "start revwalk");
            return r;
        }(),
        [](git_revwalk* r) { ::git_revwalk_free(r); }
    );

    git_oid oid;

    CheckError(git_reference_name_to_id(&oid, repo_, git_reference_name(ref.get())), "resolve reference");
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE);
    git_revwalk_push(walk.get(), &oid);

    while ((git_revwalk_next(&oid, walk.get())) == 0) {
        switch (cb(HashId::FromBytes(oid.id, sizeof(oid.id)))) {
            case WalkAction::Continue:
            case WalkAction::Hide:
                break;
            case WalkAction::Stop:
                return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

Converter::Converter(const std::filesystem::path& path, const Options& options)
    : impl_(std::make_unique<Impl>(path, options)) {
}

Converter::~Converter() = default;

Converter& Converter::SetRemap(std::function<HashId(const HashId&)> remap) {
    impl_->SetRemap(std::move(remap));
    return *this;
}

HashId Converter::ConvertCommit(const HashId& id, Datastore odb) {
    return impl_->ConvertCommit(id, std::move(odb));
}

void Converter::ListCommits(const std::string& head, const std::function<WalkAction(const HashId&)>& cb)
    const {
    impl_->ListCommits(head, cb);
}

} // namespace Vcs::Git
