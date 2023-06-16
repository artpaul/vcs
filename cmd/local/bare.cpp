#include "bare.h"
#include "fetch.h"
#include "worktree.h"

#include <vcs/changes/revwalk.h>
#include <vcs/common/config.h>
#include <vcs/store/loose.h>
#include <vcs/store/memory.h>
#include <vcs/store/pack.h>

#include <util/file.h>

#include <contrib/json/nlohmann.hpp>

namespace Vcs {
namespace {

nlohmann::json GetDefaultConfig() {
    return nlohmann::json::object({
        // Coloring mode.
        {"color", {{"ui", "auto"}}},
        // Default pager.
        {"core", {{"pager", ""}}},
    });
}

} // namespace

auto BranchInfo::Load(const std::string_view data) -> BranchInfo {
    auto json = nlohmann::json::parse(data);

    return BranchInfo{
        .name = json["name"].get<std::string>(), .head = HashId::FromHex(json["head"].get<std::string>())};
}

auto BranchInfo::Save(const BranchInfo& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["head"] = rec.head.ToHex();

    return json.dump();
}

auto RemoteInfo::Load(const std::string_view data) -> RemoteInfo {
    auto json = nlohmann::json::parse(data);

    RemoteInfo remote;
    remote.name = json["name"].get<std::string>();
    remote.fetch_uri = json["fetch"]["uri"].get<std::string>();
    remote.is_git = json["fetch"]["is_git"].get<bool>();
    return remote;
}

auto RemoteInfo::Save(const RemoteInfo& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["fetch"]["uri"] = rec.fetch_uri;
    json["fetch"]["is_git"] = rec.is_git;

    return json.dump();
}

auto WorkspaceInfo::Load(const std::string_view data) -> WorkspaceInfo {
    auto json = nlohmann::json::parse(data);

    return WorkspaceInfo{.name = json["name"].get<std::string>(), .path = json["path"].get<std::string>()};
}

auto WorkspaceInfo::Save(const WorkspaceInfo& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["path"] = rec.path.string();

    return json.dump();
}

Repository::Repository(const std::filesystem::path& path, const Options& options)
    : bare_path_(path)
    , layout_(path)
    , read_only_(options.read_only) {
    const auto lmdb_options = Lmdb::Options{.read_only = options.read_only};

    // Bulk upload cannot be done in read-only mode.
    assert(!options.bulk_upload || !options.read_only);

    // Open configs.
    config_ = std::make_unique<Config>();
    // Setup default configuration.
    config_->Reset(ConfigLocation::Default, Config::MakeBackend(GetDefaultConfig()));
    // Setup repository-level configuration.
    config_->Reset(ConfigLocation::Repository, Config::MakeBackend(layout_.Configs() / "config.json"));

    // Open object database.
    odb_ = OpenObjects(options);

    // Open branch database.
    branches_ = std::make_unique<Database<BranchInfo>>(layout_.Database("branches"), lmdb_options);

    // Open remotes database.
    remotes_ = std::make_unique<Database<RemoteInfo>>(layout_.Database("remotes"), lmdb_options);

    // Open workspace database.
    workspaces_ = std::make_unique<Database<WorkspaceInfo>>(layout_.Database("workspaces"), lmdb_options);
}

Repository::~Repository() {
    for (const auto& f : finalizers_) {
        f();
    }
}

void Repository::Initialize(const std::filesystem::path& path) {
    const Layout layout(path);

    // Root directory.
    std::filesystem::create_directories(path);

    // Top-level directories.
    std::filesystem::create_directory(layout.Configs());
    std::filesystem::create_directory(layout.Databases());
    std::filesystem::create_directory(layout.Remotes());
    std::filesystem::create_directory(layout.Workspaces());

    // Databases.
    std::filesystem::create_directory(layout.Database("branches"));
    std::filesystem::create_directory(layout.Database("remotes"));
    std::filesystem::create_directory(layout.Database("workspaces"));

    // Storages.
    std::filesystem::create_directories(layout.Objects());

    // Initialize branch database.
    std::make_unique<Database<BranchInfo>>(layout.Database("branches")).reset(nullptr);
    // Initialize workspace database.
    std::make_unique<Database<WorkspaceInfo>>(layout.Database("workspaces")).reset(nullptr);
}

Layout Repository::GetLayout() const {
    return layout_;
}

BranchInfo Repository::CreateBranch(const std::string& name, const HashId head) {
    auto branch = BranchInfo{.name = name, .head = head};

    if (auto status = branches_->Put(name, branch); !status) {
        throw std::runtime_error(
            fmt::format("cannot create branch '{}' reason '{}'", name, status.Message())
        );
    }

    return branch;
}

void Repository::DeleteBranch(const std::string_view name) {
    branches_->Delete(name);
}

std::optional<BranchInfo> Repository::GetBranch(const std::string_view name) const {
    if (auto branch = branches_->Get(name)) {
        return branch.value();
    }
    return std::nullopt;
}

void Repository::ListBranches(const std::function<void(const BranchInfo& branch)>& cb) const {
    if (!cb) {
        return;
    }
    branches_->Enumerate([&](const std::string_view, const BranchInfo& branch) {
        cb(branch);
        return true;
    });
}

const Config& Repository::GetConfig() const {
    return *config_;
}

bool Repository::HasPath(const HashId& rev, const std::string_view path) const {
    return bool(StageArea(odb_, odb_.LoadCommit(rev).Tree()).GetEntry(path));
}

void Repository::Log(
    const LogOptions& options, const std::function<bool(const HashId& id, const Commit& commit)>& cb
) const {
    if (!cb || options.roots.empty()) {
        return;
    }

    RevisionGraph::Walker(RevisionGraph(odb_))
        .Hide(options.hidden)
        .Push(options.roots)
        .SimplifyFirstParent(options.first_parent)
        .Walk([&](const RevisionGraph::Revision& r) {
            if (cb(r.Id(), odb_.LoadCommit(r.Id()))) {
                return WalkAction::Continue;
            } else {
                return WalkAction::Stop;
            }
        });
}

void Repository::PathLog(
    const LogOptions& options,
    const std::string_view path,
    const std::function<bool(const HashId& id, const std::string_view, const Commit&)>& cb
) const {
    if (!cb || options.roots.empty()) {
        return;
    }

    if (path.empty()) {
        return Log(options, [&](const HashId& id, const Commit& commit) {
            return cb(id, std::string_view(), commit);
        });
    }

    //
    auto odb = odb_.Cache(Store::MemoryCache::Make());
    //
    std::optional<std::pair<HashId, PathEntry>> prev;

    RevisionGraph::Walker(RevisionGraph(odb_))
        .Hide(options.hidden)
        .Push(options.roots)
        .SimplifyFirstParent(true)
        .Walk([&](const RevisionGraph::Revision& r) {
            const auto entry = StageArea(odb, r.Tree()).GetEntry(path);

            if (!bool(entry)) {
                return WalkAction::Stop;
            }

            if (prev) {
                // Entries are equal. Continue.
                if (!CompareEntries(prev->second, *entry)) {
                    prev->first = r.Id();
                    return WalkAction::Continue;
                }
                // Report changes. Stop if requester.
                if (!cb(prev->first, path, odb_.LoadCommit(prev->first))) {
                    return WalkAction::Stop;
                }
            }
            // Update the changepoint.
            prev = std::make_pair(r.Id(), *entry);

            return WalkAction::Continue;
        });

    if (prev) {
        cb(prev->first, path, odb_.LoadCommit(prev->first));
    }
}

Datastore Repository::Objects() {
    return odb_;
}

const Datastore Repository::Objects() const {
    return odb_;
}

bool Repository::CreateRemote(const RemoteInfo& remote) {
    // Check there is no remote with same name.
    if (remotes_->Get(remote.name)) {
        return false;
    }

    if (remotes_->Put(remote.name, remote)) {
        // Create directory for branches database.
        std::filesystem::create_directories(layout_.Remote(remote.name));
        return true;
    } else {
        return false;
    }
}

void Repository::ListRemotes(const std::function<void(const RemoteInfo&)>& cb) const {
    if (!cb) {
        return;
    }
    remotes_->Enumerate([&](const std::string_view, const RemoteInfo& remote) {
        cb(remote);
        return true;
    });
}

std::unique_ptr<Database<BranchInfo>> Repository::GetRemoteBranches(const std::string_view name) const {
    if (remotes_->Get(name)) {
        return std::make_unique<Database<BranchInfo>>(
            layout_.Remote(name), Lmdb::Options{.read_only = read_only_}
        );
    }
    return {};
}

std::unique_ptr<Fetcher> Repository::GetRemoteFetcher(const std::string_view name) const {
    if (const auto remote = remotes_->Get(name)) {
        if (remote->is_git) {
            return CreateGitFetcher(remote->name, remote->fetch_uri, this);
        }
    }
    return {};
}

bool Repository::CreateWorkspace(const WorkspaceInfo& params, bool checkout) {
    // Check there is no workspace with same path.
    if (workspaces_->Get(params.path.string())) {
        return false;
    }

    const auto branch = branches_->Get(params.branch);

    if (!branch) {
        return false;
    }

    const auto state_path = layout_.Workspace(params.name);
    const auto tree =
        params.tree ? params.tree : (branch->head ? odb_.LoadCommit(branch->head).Tree() : HashId());

    // Create state path.
    std::filesystem::create_directory(state_path);

    // Write head.
    StringToFile(state_path / "HEAD", params.branch);

    // Create working tree path.
    std::filesystem::create_directories(params.path);
    // Save info into the database.
    workspaces_->Put(params.path.string(), params);

    // Checkout revision's content.
    if (checkout) {
        WorkingTree(params.path, odb_, [tree]() { return tree; }).Checkout(tree);
    }

    return true;
}

std::optional<WorkspaceInfo> Repository::GetWorkspace(const std::string& name) const {
    if (auto ws = workspaces_->Get(name)) {
        const auto state_path = layout_.Workspace(ws->name);

        ws->branch = StringFromFile(state_path / "HEAD");

        if (const auto branch = branches_->Get(ws->branch)) {
            ws->tree = branch->head ? odb_.LoadCommit(branch->head).Tree() : HashId();
        }

        return ws.value();
    }

    return std::nullopt;
}

Datastore Repository::OpenObjects(const Options& options) {
    if (options.bulk_upload) {
        auto pack = Store::Leveled::Make(layout_.Packs(), Store::Leveled::Options{.read_only = false});

        // Pack all written objects at the end.
        finalizers_.emplace_back([pack] { pack->Pack(); });

        return Datastore().Chain(pack);
    }

    // Loose storage.
    Datastore odb = Datastore::Make<Store::Loose>(layout_.Objects());
    // Pack storage.
    if (std::filesystem::exists(layout_.Packs())) {
        odb = odb.Chain(Store::Leveled::Make(layout_.Packs(), Store::Leveled::Options{.read_only = true}));
    }

    return odb;
}

} // namespace Vcs
