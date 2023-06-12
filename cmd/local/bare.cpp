#include "bare.h"
#include "config.h"
#include "worktree.h"

#include <vcs/changes/revwalk.h>
#include <vcs/store/loose.h>

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

auto Repository::Branch::Load(const std::string_view data) -> Branch {
    auto json = nlohmann::json::parse(data);

    return Branch{
        .name = json["name"].get<std::string>(), .head = HashId::FromHex(json["head"].get<std::string>())};
}

auto Repository::Branch::Save(const Branch& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["head"] = rec.head.ToHex();

    return json.dump();
}

auto Repository::Remote::Load(const std::string_view data) -> Remote {
    auto json = nlohmann::json::parse(data);

    Remote remote;
    remote.name = json["name"].get<std::string>();
    remote.fetch_uri = json["fetch"]["uri"].get<std::string>();
    remote.is_git = json["fetch"]["is_git"].get<bool>();
    return remote;
}

auto Repository::Remote::Save(const Remote& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["fetch"]["uri"] = rec.fetch_uri;
    json["fetch"]["is_git"] = rec.is_git;

    return json.dump();
}

auto Repository::Workspace::Load(const std::string_view data) -> Workspace {
    auto json = nlohmann::json::parse(data);

    return Workspace{.name = json["name"].get<std::string>(), .path = json["path"].get<std::string>()};
}

auto Repository::Workspace::Save(const Workspace& rec) -> std::string {
    auto json = nlohmann::json::object();

    json["name"] = rec.name;
    json["path"] = rec.path.string();

    return json.dump();
}

Repository::Repository(const std::filesystem::path& path)
    : bare_path_(path)
    , layout_(path) {
    // Open configs.
    config_ = std::make_unique<Config>();
    // Setup default configuration.
    config_->Reset(ConfigLocation::Default, Config::MakeBackend(GetDefaultConfig()));
    // Setup repository-level configuration.
    config_->Reset(ConfigLocation::Repository, Config::MakeBackend(path / "config" / "config.json"));

    // Open object database.
    odb_ = Datastore::Make<Store::Loose>(path / "objects");

    // Open branch database.
    branches_ = std::make_unique<Database<Branch>>(layout_.Database("branches"));

    // Open remotes database.
    remotes_ = std::make_unique<Database<Remote>>(layout_.Database("remotes"));

    // Open workspace database.
    workspaces_ = std::make_unique<Database<Workspace>>(layout_.Database("workspaces"));
}

Repository::~Repository() = default;

void Repository::Initialize(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);

    std::filesystem::create_directory(path / "config");
    std::filesystem::create_directory(path / "db");
    std::filesystem::create_directory(path / "db" / "branches");
    std::filesystem::create_directory(path / "db" / "remotes");
    std::filesystem::create_directory(path / "db" / "workspaces");
    std::filesystem::create_directory(path / "remotes");
    std::filesystem::create_directory(path / "objects");
    std::filesystem::create_directory(path / "workspaces");

    // Initialize branch database.
    std::make_unique<Database<Branch>>(path / "db" / "branches").reset(nullptr);
    // Initialize workspace database.
    std::make_unique<Database<Workspace>>(path / "db" / "workspaces").reset(nullptr);
}

Layout Repository::GetLayout() const {
    return layout_;
}

Repository::Branch Repository::CreateBranch(const std::string& name, const HashId head) {
    auto branch = Branch{.name = name, .head = head};

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

std::optional<Repository::Branch> Repository::GetBranch(const std::string_view name) const {
    if (auto branch = branches_->Get(name)) {
        return branch.value();
    }
    return std::nullopt;
}

void Repository::ListBranches(const std::function<void(const Branch& branch)>& cb) const {
    if (!cb) {
        return;
    }
    branches_->Enumerate([&](const std::string_view, const Branch& branch) {
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

Datastore Repository::Objects() {
    return odb_;
}

const Datastore Repository::Objects() const {
    return odb_;
}

bool Repository::CreateRemote(const Remote& remote) {
    // Check there is no remote with same name.
    if (remotes_->Get(remote.name)) {
        return false;
    }

    if (remotes_->Put(remote.name, remote)) {
        // Create directory for branches database.
        std::filesystem::create_directories(layout_.Remotes() / remote.name);
        return true;
    } else {
        return false;
    }
}

void Repository::ListRemotes(const std::function<void(const Remote&)>& cb) const {
    if (!cb) {
        return;
    }
    remotes_->Enumerate([&](const std::string_view, const Remote& remote) {
        cb(remote);
        return true;
    });
}

std::unique_ptr<Database<Repository::Branch>> Repository::GetRemoteBranches(const std::string_view name
) const {
    return std::make_unique<Database<Branch>>(layout_.Remotes() / name);
}

bool Repository::CreateWorkspace(const Workspace& params, bool checkout) {
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

std::optional<Repository::Workspace> Repository::GetWorkspace(const std::string& name) const {
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

} // namespace Vcs
