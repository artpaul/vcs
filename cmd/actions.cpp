#include "actions.h"

#include <unordered_map>

namespace Vcs {

static const std::unordered_map<std::string_view, Action> actions = {
    // Actions.
    {"branch", Action::Branch},
    {"clean", Action::Clean},
    {"commit", Action::Commit},
    {"config", Action::Config},
    {"diff", Action::Diff},
    {"fetch", Action::Fetch},
    {"init", Action::Init},
    {"log", Action::Log},
    {"remote", Action::Remote},
    {"reset", Action::Reset},
    {"restore", Action::Restore},
    {"rm", Action::Remove},
    {"show", Action::Show},
    {"status", Action::Status},
    {"switch", Action::Switch},
    {"workspace", Action::Workspace},

    // Shortcuts.
    {"br", Action::Branch},
    {"ci", Action::Commit},
    {"st", Action::Status},
    {"sw", Action::Switch},
    {"ws", Action::Workspace},

    // Tools.
    {"dump", Action::Dump},
    {"git", Action::Git},
};

Action ParseAction(const std::string_view name) noexcept {
    if (auto ai = actions.find(name); ai != actions.end()) {
        return ai->second;
    }
    return Action::Unknown;
}

} // namespace Vcs
