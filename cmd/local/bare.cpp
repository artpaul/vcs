#include "bare.h"
#include "worktree.h"

#include <vcs/changes/revwalk.h>
#include <vcs/store/loose.h>

#include <util/file.h>

#include <contrib/json/nlohmann.hpp>

namespace Vcs {

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
    : bare_path_(path) {
    // Open object database.
    odb_ = Datastore::Make<Store::Loose>(path / "objects");
    // Open branch database.
    branches_ = std::make_unique<Database<Branch>>(path / "db" / "branches");
    // Open workspace database.
    workspaces_ = std::make_unique<Database<Workspace>>(path / "db" / "workspaces");
}

void Repository::Initialize(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);

    std::filesystem::create_directory(path / "config");
    std::filesystem::create_directory(path / "db");
    std::filesystem::create_directory(path / "db" / "workspaces");
    std::filesystem::create_directory(path / "db" / "branches");
    std::filesystem::create_directory(path / "objects");
    std::filesystem::create_directory(path / "workspaces");

    // Initialize branch database.
    std::make_unique<Database<Branch>>(path / "db" / "branches").reset(nullptr);
    // Initialize workspace database.
    std::make_unique<Database<Workspace>>(path / "db" / "workspaces").reset(nullptr);
}

void Repository::CreateBranch(const std::string& name, const HashId head) {
    branches_->Put(name, Branch{.name = name, .head = head});
}

void Repository::DeleteBranch(const std::string& name) {
    branches_->Delete(name);
}

std::optional<Repository::Branch> Repository::GetBranch(const std::string& name) const {
    return branches_->Get(name);
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

bool Repository::CreateWorkspace(const Workspace& params, bool checkout) {
    // Check there is no workspace with same path.
    if (workspaces_->Get(params.path.string())) {
        return false;
    }

    const auto branch = branches_->Get(params.branch);

    if (!branch) {
        return false;
    }

    const auto state_path = bare_path_ / "workspaces" / params.name;
    const auto tree =
        params.tree ? params.tree : (branch->head ? odb_.LoadCommit(branch->head).Tree() : HashId());

    // Create state path.
    std::filesystem::create_directory(state_path);

    // Write head.
    StringToFile(state_path / "HEAD", params.branch);
    // Write base tree.
    StringToFile(state_path / "TREE", tree.ToHex());

    // Create working tree path.
    std::filesystem::create_directories(params.path);
    // Save info into the database.
    workspaces_->Put(params.path.string(), params);

    // Checkout revision's content.
    if (checkout) {
        WorkingTree(params.path, odb_).Checkout(tree);
    }

    return true;
}

std::optional<Repository::Workspace> Repository::GetWorkspace(const std::string& name) const {
    if (auto ws = workspaces_->Get(name)) {
        const auto state_path = bare_path_ / "workspaces" / ws->name;

        ws->branch = StringFromFile(state_path / "HEAD");
        ws->tree = HashId::FromHex(StringFromFile(state_path / "TREE"));

        return ws;
    }

    return std::nullopt;
}

} // namespace Vcs
