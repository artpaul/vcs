#include "pager.h"

#include <vcs/common/config.h>

#include <util/split.h>
#include <util/tty.h>

#include <contrib/fmt/fmt/format.h>
#include <contrib/subprocess/subprocess.hpp>

#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/ioctl.h>

namespace Vcs {
namespace {

static constexpr std::string_view kDefaultPager = "less -rFX";

static subprocess::Popen gPagerProcess;

void ClosePagerFds() {
    // Signal EOF to pager.
    ::close(1);
    ::close(2);
}

void WaitForPagerAtExit() {
    ::fflush(stdout);
    ::fflush(stderr);

    ClosePagerFds();

    gPagerProcess.close();
}

std::string GetPager(const Config& config) {
    std::optional<std::string> pager;

    if (!IsAtty(stdout)) {
        return {};
    }
    // TODO: from env (VCS_PAGER)
    if (const auto value = config.Get("core.pager")) {
        if (value->is_string()) {
            pager = value->get<std::string>();
        }
    }
    // TODO: from env (PAGER)
    if (!pager) {
        pager = kDefaultPager;
    }
    if (pager->empty() || *pager == "cat") {
        return {};
    }

    return *pager;
}

std::pair<int, bool> TerminalColumns() {
    static int term_columns_at_startup;
    static bool term_columns_guessed;

    char* col_string;
    int n_cols;

    if (term_columns_at_startup) {
        return std::make_pair(term_columns_at_startup, term_columns_guessed);
    }

    term_columns_at_startup = 80;
    term_columns_guessed = true;
    col_string = std::getenv("COLUMNS");
    if (col_string && (n_cols = std::atoi(col_string)) > 0) {
        term_columns_at_startup = n_cols;
        term_columns_guessed = false;
    }
#ifdef TIOCGWINSZ
    else
    {
        struct winsize ws;
        if (!::ioctl(1, TIOCGWINSZ, &ws) && ws.ws_col) {
            term_columns_at_startup = ws.ws_col;
            term_columns_guessed = false;
        }
    }
#endif

    return std::make_pair(term_columns_at_startup, term_columns_guessed);
}

} // namespace

bool PagerInUse() noexcept {
    if (const char* value = ::getenv("VCS_PAGER_IN_USE")) {
        return std::strcmp(value, "true") == 0;
    }
    return false;
}

void SetupPager(const Config& config) {
    const std::string pager = GetPager(config);

    if (pager.empty()) {
        return;
    }

    if (auto [columns, guessed] = TerminalColumns(); !guessed) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", columns);
        ::setenv("COLUMNS", buf, 0);
    }

    ::setenv("VCS_PAGER_IN_USE", "true", 1);

    gPagerProcess = subprocess::RunBuilder(SplitString<std::string>(pager, ' '))
                        .cin(subprocess::PipeOption::pipe)
                        .popen();
    if (gPagerProcess.pid == 0) {
        return;
    }

    ::dup2(gPagerProcess.cin, 1);
    if (IsAtty(stderr)) {
        ::dup2(gPagerProcess.cin, 2);
    }
    ::close(gPagerProcess.cin);

    ::atexit(WaitForPagerAtExit);
}

} // namespace Vcs
