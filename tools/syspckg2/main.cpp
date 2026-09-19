#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/common/sack/query_cmp.hpp>
#include <libdnf5/repo/download_callbacks.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/transaction/transaction_item_action.hpp>
#include <libdnf5/utils/locker.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

namespace {

constexpr const char * VERSION = "0.2.0";

constexpr const char * COLOR_RED = "\033[31m";
constexpr const char * COLOR_YELLOW = "\033[33m";
constexpr const char * COLOR_GREEN = "\033[32m";
constexpr const char * COLOR_CYAN = "\033[36m";
constexpr const char * COLOR_RESET = "\033[0m";

void log_err(const std::string & message) {
    std::cerr << COLOR_RED << "ERR: " << COLOR_RESET << message << "\n";
}

void log_warn(const std::string & message) {
    std::cerr << COLOR_YELLOW << "WARN: " << COLOR_RESET << message << "\n";
}

void log_ok(const std::string & message) {
    std::cout << COLOR_GREEN << "OK: " << COLOR_RESET << message << "\n";
}

void log_info(const std::string & message) {
    std::cout << COLOR_CYAN << "INFO: " << COLOR_RESET << message << "\n";
}

std::string format_bytes(double bytes) {
    if (bytes <= 0.0) {
        return "unknown size";
    }

    static constexpr const char * units[] = {"B", "KiB", "MiB", "GiB"};
    std::size_t unit = 0;
    while (bytes >= 1024.0 && unit + 1 < std::size(units)) {
        bytes /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << bytes << " " << units[unit];
    return out.str();
}

class VerboseDownloadCallbacks : public libdnf5::repo::DownloadCallbacks {
public:
    struct Result {
        std::size_t successful{0};
        std::size_t failed{0};
        std::string last_error;
    };

    const Result * result_for(const std::string & description) const {
        const auto it = results_.find(description);
        return it == results_.end() ? nullptr : &it->second;
    }

private:
    struct DownloadState {
        std::string description;
        double total{0.0};
        int last_bucket{-10};
    };

    void * add_new_download(
        [[maybe_unused]] void * user_data,
        const char * description,
        double total_to_download) override {
        downloads_.push_back(DownloadState{
            description ? description : "(unknown download)",
            total_to_download,
            -10,
        });
        auto & state = downloads_.back();

        log_info(
            "Fetch start: " + state.description +
            " (" + format_bytes(total_to_download) + ")");
        return &state;
    }

    int progress(void * user_cb_data, double total_to_download, double downloaded) override {
        auto * state = static_cast<DownloadState *>(user_cb_data);
        if (!state || total_to_download <= 0.0) {
            return OK;
        }

        int percent = static_cast<int>((downloaded * 100.0) / total_to_download);
        percent = std::clamp(percent, 0, 100);
        const int bucket = (percent / 10) * 10;

        if (bucket >= state->last_bucket + 10 || percent == 100) {
            state->last_bucket = bucket;
            log_info(
                "Fetch progress: " + state->description + " " +
                std::to_string(percent) + "% (" +
                format_bytes(downloaded) + "/" + format_bytes(total_to_download) + ")");
        }

        return OK;
    }

    int end(void * user_cb_data, TransferStatus status, const char * msg) override {
        auto * state = static_cast<DownloadState *>(user_cb_data);
        const std::string description = state ? state->description : "(unknown download)";
        auto & result = results_[description];

        switch (status) {
            case TransferStatus::SUCCESSFUL:
                ++result.successful;
                log_ok("Fetch complete: " + description);
                break;
            case TransferStatus::ALREADYEXISTS:
                ++result.successful;
                log_info("Fetch cache hit: " + description);
                break;
            case TransferStatus::ERROR: {
                ++result.failed;
                result.last_error = msg ? msg : "unknown download error";
                log_warn("Fetch failed: " + description + " -> " + result.last_error);
                break;
            }
        }

        return OK;
    }

    int mirror_failure(
        [[maybe_unused]] void * user_cb_data,
        const char * msg,
        const char * url,
        const char * metadata) override {
        std::string detail = "Mirror failed";
        if (url && *url) {
            detail += ": ";
            detail += url;
        }
        if (metadata && *metadata) {
            detail += " [";
            detail += metadata;
            detail += "]";
        }
        if (msg && *msg) {
            detail += " -> ";
            detail += msg;
        }
        log_warn(detail);
        return OK;
    }

    void fastest_mirror(
        [[maybe_unused]] void * user_cb_data,
        FastestMirrorStage stage,
        const char * ptr) override {
        if (stage == FastestMirrorStage::DETECTION && ptr) {
            const auto count = *reinterpret_cast<const long *>(ptr);
            log_info("Testing " + std::to_string(count) + " repository mirrors...");
        } else if (stage == FastestMirrorStage::STATUS && ptr && *ptr) {
            log_warn(std::string("Mirror detection failed: ") + ptr);
        }
    }

    std::list<DownloadState> downloads_;
    std::unordered_map<std::string, Result> results_;
};

enum class SourceMode {
    AUTO,
    ADAVA,
    FEDORA,
};

struct Options {
    std::string command;
    std::vector<std::string> args;
    SourceMode source{SourceMode::AUTO};
    bool assume_yes{false};
};

void banner() {
    std::cerr << COLOR_GREEN << "SystemPackager 2" << COLOR_RESET
              << " by Adava Software for Linux in 2026 "
              << COLOR_YELLOW << "v" << VERSION << COLOR_RESET << "\n";
}

void usage(const char * prog) {
    std::cerr
        << COLOR_YELLOW << "Usage:" << COLOR_RESET << "\n"
        << "  " << prog << " install <package>... [--source auto|adava|fedora] [-y]\n"
        << "  " << prog << " remove <package>... [-y]\n"
        << "  " << prog << " update [package]... [--source auto|adava|fedora] [-y]\n"
        << "  " << prog << " upgrade [package]... [--source auto|adava|fedora] [-y]\n"
        << "  " << prog << " search <term>... [--source auto|adava|fedora]\n"
        << "  " << prog << " info <package>... [--source auto|adava|fedora]\n"
        << "  " << prog << " list [installed] [--source auto|adava|fedora]\n"
        << "  " << prog << " repos\n"
        << "  " << prog << " clean\n"
        << "\n"
        << "SystemPackager 2 talks directly to libdnf5. It does not execute dnf5.\n"
        << "Sources: auto (AdavaLinux + Fedora), adava, fedora.\n";
}

bool is_command(const std::string & value) {
    static const std::set<std::string> commands{
        "install", "remove", "update", "upgrade", "search", "info", "list", "repos", "clean"};
    return commands.count(value) != 0;
}

SourceMode parse_source(const std::string & value) {
    if (value == "auto") {
        return SourceMode::AUTO;
    }
    if (value == "adava" || value == "adavalinux") {
        return SourceMode::ADAVA;
    }
    if (value == "fedora") {
        return SourceMode::FEDORA;
    }
    throw std::runtime_error("Unknown source: " + value);
}

Options parse_options(int argc, char ** argv) {
    Options opts;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg{argv[i]};
        if (arg == "-y" || arg == "--assumeyes") {
            opts.assume_yes = true;
        } else if (arg == "--adava") {
            opts.source = SourceMode::ADAVA;
        } else if (arg == "--fedora") {
            opts.source = SourceMode::FEDORA;
        } else if (arg.rfind("--source=", 0) == 0) {
            opts.source = parse_source(arg.substr(9));
        } else if (arg == "--source") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--source expects auto, adava or fedora");
            }
            opts.source = parse_source(argv[++i]);
        } else {
            positional.push_back(std::move(arg));
        }
    }

    if (positional.empty()) {
        throw std::runtime_error("No command specified");
    }

    if (!is_command(positional.front())) {
        throw std::runtime_error(
            "Unknown command '" + positional.front() +
            "'. Package names must follow an explicit command, for example: syspckg2 install " +
            positional.front());
    }

    opts.command = positional.front();
    opts.args.assign(positional.begin() + 1, positional.end());
    return opts;
}

bool is_transaction_command(const std::string & command) {
    return command == "install" || command == "remove" || command == "update" || command == "upgrade";
}

bool has_problem(libdnf5::GoalProblem problems, libdnf5::GoalProblem flag) {
    return (static_cast<std::uint32_t>(problems) & static_cast<std::uint32_t>(flag)) != 0;
}

bool fatal_resolve_problem(libdnf5::GoalProblem problems) {
    return has_problem(problems, libdnf5::GoalProblem::SOLVER_ERROR) ||
           has_problem(problems, libdnf5::GoalProblem::NOT_FOUND) ||
           has_problem(problems, libdnf5::GoalProblem::NOT_FOUND_IN_REPOSITORIES) ||
           has_problem(problems, libdnf5::GoalProblem::NOT_INSTALLED) ||
           has_problem(problems, libdnf5::GoalProblem::NOT_AVAILABLE) ||
           has_problem(problems, libdnf5::GoalProblem::UNSUPPORTED_ACTION);
}

std::vector<std::string> selected_repo_ids(SourceMode source) {
    switch (source) {
        case SourceMode::ADAVA:
            return {"adavalinux"};
        case SourceMode::FEDORA:
            return {"fedora", "updates"};
        case SourceMode::AUTO:
            return {};
    }
    return {};
}

void apply_repo_selection(libdnf5::Base & base, SourceMode source) {
    if (source == SourceMode::AUTO) {
        return;
    }

    const auto allowed = selected_repo_ids(source);
    libdnf5::repo::RepoQuery repos(base);
    for (auto repo : repos) {
        if (repo->get_type() != libdnf5::repo::Repo::Type::AVAILABLE) {
            continue;
        }
        if (std::find(allowed.begin(), allowed.end(), repo->get_id()) == allowed.end()) {
            repo->disable();
        } else {
            repo->enable();
        }
    }
}

void check_runtime_layout() {
    if (!std::filesystem::exists("/usr/lib/rpm/rpmrc")) {
        throw std::runtime_error("RPM configuration is missing: /usr/lib/rpm/rpmrc");
    }
    if (!std::filesystem::exists("/usr/lib/rpm/macros")) {
        throw std::runtime_error("RPM macros are missing: /usr/lib/rpm/macros");
    }
}

void prepare_base(libdnf5::Base & base, SourceMode source, bool write_lock, bool load_repositories = true) {
    check_runtime_layout();

    auto & config = base.get_config();
    config.get_plugins_option().set(false);
    config.get_installroot_option().set("/");

    // Loading dnf.conf is optional. AdavaLinux intentionally keeps the package
    // policy in the vendor .repo files and libdnf5 defaults.
    log_info("Preparing RPM/libdnf5 backend...");
    base.setup();

    auto repo_sack = base.get_repo_sack();
    repo_sack->create_repos_from_system_configuration();
    apply_repo_selection(base, source);

    if (!load_repositories) {
        return;
    }

    std::vector<std::string> enabled_repo_ids;
    {
        libdnf5::repo::RepoQuery repos(base);
        for (auto repo : repos) {
            if (repo->get_type() != libdnf5::repo::Repo::Type::AVAILABLE || !repo->is_enabled()) {
                continue;
            }

            const auto repo_id = repo->get_id();
            enabled_repo_ids.push_back(repo_id);

            auto & repo_config = repo->get_config();
            repo_config.get_skip_if_unavailable_option().set(true);

            const auto & metalink = repo_config.get_metalink_option();
            const auto & mirrorlist = repo_config.get_mirrorlist_option();
            const auto & baseurls = repo_config.get_baseurl_option().get_value();

            if (!metalink.empty() && !metalink.get_value().empty()) {
                log_info("Repository " + repo_id + ": metalink " + metalink.get_value());
            } else if (!mirrorlist.empty() && !mirrorlist.get_value().empty()) {
                log_info("Repository " + repo_id + ": mirrorlist " + mirrorlist.get_value());
            } else if (!baseurls.empty()) {
                for (const auto & url : baseurls) {
                    log_info("Repository " + repo_id + ": baseurl " + url);
                }
            } else {
                log_warn("Repository " + repo_id + " has no configured download source");
            }
        }
    }

    if (enabled_repo_ids.empty()) {
        throw std::runtime_error("No enabled package repositories");
    }

    auto download_callbacks = std::make_unique<VerboseDownloadCallbacks>();
    auto * download_callbacks_ptr = download_callbacks.get();
    base.set_download_callbacks(std::move(download_callbacks));

    base.lock_system_repo(
        write_lock ? libdnf5::utils::LockAccess::WRITE : libdnf5::utils::LockAccess::READ,
        libdnf5::utils::LockBlocking::BLOCKING);

    log_info("Loading repository metadata...");
    repo_sack->load_repos();

    std::size_t usable_repositories = 0;
    for (const auto & repo_id : enabled_repo_ids) {
        const auto * result = download_callbacks_ptr->result_for(repo_id);

        // A cache hit or a successful metadata transfer means the repository
        // is usable. If libdnf5 did not invoke the callback (for example when
        // metadata was already loaded internally), verify package visibility.
        bool usable = result && result->successful > 0;
        if (!usable && (!result || result->failed == 0)) {
            libdnf5::rpm::PackageQuery repo_packages(base);
            repo_packages.filter_available();
            repo_packages.filter_repo_id(repo_id);
            usable = !repo_packages.empty();
        }

        if (!usable) {
            std::string message = "Repository failed: " + repo_id;
            if (result && !result->last_error.empty()) {
                message += " -> " + result->last_error;
            }
            message += "; continuing with remaining repositories";
            log_warn(message);
        } else {
            ++usable_repositories;
            log_ok("Repository ready: " + repo_id);
        }
    }

    if (usable_repositories == 0) {
        throw std::runtime_error("Unable to fetch any enabled package repository");
    }

    log_ok(
        "Repositories loaded (" + std::to_string(usable_repositories) + "/" +
        std::to_string(enabled_repo_ids.size()) + ")");
}

bool confirm_transaction(bool assume_yes) {
    if (assume_yes || !isatty(STDIN_FILENO)) {
        return true;
    }

    std::cerr << COLOR_YELLOW << "Continue? [Y/n] " << COLOR_RESET;
    std::string answer;
    std::getline(std::cin, answer);
    return answer.empty() || answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

void print_transaction(libdnf5::base::Transaction & transaction) {
    const auto packages = transaction.get_transaction_packages();
    if (packages.empty()) {
        return;
    }

    std::cout << "\n" << COLOR_YELLOW << "Transaction:" << COLOR_RESET << "\n";
    for (const auto & item : packages) {
        const auto action = libdnf5::transaction::transaction_item_action_to_string(item.get_action());
        const char * color = COLOR_CYAN;
        if (action.find("Install") != std::string::npos || action.find("INSTALL") != std::string::npos) {
            color = COLOR_GREEN;
        } else if (
            action.find("Remove") != std::string::npos || action.find("Erase") != std::string::npos ||
            action.find("REMOVE") != std::string::npos || action.find("ERASE") != std::string::npos) {
            color = COLOR_RED;
        } else if (
            action.find("Upgrade") != std::string::npos || action.find("Downgrade") != std::string::npos ||
            action.find("UPGRADE") != std::string::npos || action.find("DOWNGRADE") != std::string::npos) {
            color = COLOR_YELLOW;
        }

        std::cout << "  " << color << action << COLOR_RESET
                  << "  " << item.get_package().get_nevra()
                  << "  [" << item.get_package().get_repo_id() << "]\n";
    }
    std::cout << "\n";
}

int run_goal(libdnf5::Goal & goal, bool assume_yes, const std::string & description) {
    auto transaction = goal.resolve();

    for (const auto & line : transaction.get_resolve_logs_as_strings()) {
        log_warn(line);
    }

    if (fatal_resolve_problem(transaction.get_problems())) {
        log_err("Dependency resolution failed.");
        return 2;
    }

    if (transaction.empty()) {
        log_ok("Nothing to do");
        return 0;
    }

    print_transaction(transaction);
    if (!confirm_transaction(assume_yes)) {
        log_warn("Cancelled");
        return 0;
    }

    transaction.set_description(description);

    try {
        log_info("Downloading packages...");
        transaction.download();
        log_ok("Download complete");
        log_info("Running RPM transaction...");
        const auto result = transaction.run();
        if (result != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            log_err(libdnf5::base::Transaction::transaction_result_to_string(result));
            for (const auto & problem : transaction.get_transaction_problems()) {
                std::cerr << "  " << COLOR_RED << problem << COLOR_RESET << "\n";
            }
            return 3;
        }
    } catch (const std::exception & ex) {
        log_err(ex.what());
        return 3;
    }

    log_ok("Transaction completed");
    return 0;
}

int command_install(libdnf5::Base & base, const Options & opts) {
    if (opts.args.empty()) {
        throw std::runtime_error("install requires at least one package");
    }

    libdnf5::Goal goal(base);
    libdnf5::GoalJobSettings settings;
    const auto repos = selected_repo_ids(opts.source);
    if (!repos.empty()) {
        settings.set_to_repo_ids(repos);
    }

    for (const auto & spec : opts.args) {
        goal.add_rpm_install(spec, settings);
    }
    return run_goal(goal, opts.assume_yes, "syspckg2 install");
}

int command_remove(libdnf5::Base & base, const Options & opts) {
    if (opts.args.empty()) {
        throw std::runtime_error("remove requires at least one package");
    }

    libdnf5::Goal goal(base);
    for (const auto & spec : opts.args) {
        goal.add_rpm_remove(spec);
    }
    return run_goal(goal, opts.assume_yes, "syspckg2 remove");
}

int command_upgrade(libdnf5::Base & base, const Options & opts) {
    libdnf5::Goal goal(base);
    libdnf5::GoalJobSettings settings;
    const auto repos = selected_repo_ids(opts.source);
    if (!repos.empty()) {
        settings.set_to_repo_ids(repos);
    }

    if (opts.args.empty()) {
        goal.add_rpm_upgrade(settings);
    } else {
        for (const auto & spec : opts.args) {
            goal.add_rpm_upgrade(spec, settings);
        }
    }
    return run_goal(goal, opts.assume_yes, "syspckg2 upgrade");
}

void print_package_line(const libdnf5::rpm::Package & pkg) {
    std::cout << COLOR_GREEN << pkg.get_name() << COLOR_RESET << "." << pkg.get_arch()
              << "\t" << pkg.get_version() << "-" << pkg.get_release()
              << "\t" << COLOR_YELLOW << pkg.get_repo_id() << COLOR_RESET;
    const auto summary = pkg.get_summary();
    if (!summary.empty()) {
        std::cout << "\t" << summary;
    }
    std::cout << "\n";
}

int command_search(libdnf5::Base & base, const Options & opts) {
    if (opts.args.empty()) {
        throw std::runtime_error("search requires a term");
    }

    libdnf5::rpm::PackageQuery result(base, libdnf5::sack::ExcludeFlags::APPLY_EXCLUDES, true);
    for (const auto & term : opts.args) {
        libdnf5::rpm::PackageQuery names(base);
        names.filter_name(term, libdnf5::sack::QueryCmp::ICONTAINS);

        libdnf5::rpm::PackageQuery summaries(base);
        summaries.filter_summary(term, libdnf5::sack::QueryCmp::ICONTAINS);

        libdnf5::rpm::PackageQuery descriptions(base);
        descriptions.filter_description(term, libdnf5::sack::QueryCmp::ICONTAINS);

        result |= names;
        result |= summaries;
        result |= descriptions;
    }

    result.filter_priority();
    result.filter_latest_evr();

    if (result.empty()) {
        log_warn("No matching packages");
        return 1;
    }

    for (const auto & pkg : result) {
        print_package_line(pkg);
    }
    return 0;
}

int command_info(libdnf5::Base & base, const Options & opts) {
    if (opts.args.empty()) {
        throw std::runtime_error("info requires at least one package");
    }

    bool found = false;
    for (const auto & name : opts.args) {
        libdnf5::rpm::PackageQuery query(base);
        query.filter_name(name);
        query.filter_priority();
        query.filter_latest_evr();

        for (const auto & pkg : query) {
            found = true;
            std::cout << COLOR_GREEN << "Name:        " << COLOR_RESET << pkg.get_name() << "\n"
                      << COLOR_GREEN << "Version:     " << COLOR_RESET << pkg.get_version() << "-" << pkg.get_release() << "\n"
                      << COLOR_GREEN << "Architecture:" << COLOR_RESET << " " << pkg.get_arch() << "\n"
                      << COLOR_GREEN << "Repository:  " << COLOR_RESET << COLOR_YELLOW << pkg.get_repo_id() << COLOR_RESET << "\n"
                      << "License:     " << pkg.get_license() << "\n"
                      << "Summary:     " << pkg.get_summary() << "\n"
                      << "Description: " << pkg.get_description() << "\n\n";
        }
    }

    return found ? 0 : 1;
}

int command_list(libdnf5::Base & base, const Options & opts) {
    libdnf5::rpm::PackageQuery query(base);
    if (!opts.args.empty() && opts.args.front() == "installed") {
        query.filter_installed();
    }
    query.filter_priority();
    query.filter_latest_evr();

    for (const auto & pkg : query) {
        print_package_line(pkg);
    }
    return 0;
}

int command_repos(libdnf5::Base & base) {
    libdnf5::repo::RepoQuery repos(base);
    for (auto repo : repos) {
        if (repo->get_type() == libdnf5::repo::Repo::Type::SYSTEM) {
            continue;
        }
        std::cout << COLOR_GREEN << repo->get_id() << COLOR_RESET << "\t"
                  << (repo->is_enabled() ? COLOR_GREEN : COLOR_RED)
                  << (repo->is_enabled() ? "enabled" : "disabled")
                  << COLOR_RESET << "\t"
                  << repo->get_config().get_name_option().get_value() << "\n";
    }
    return 0;
}

int command_clean(libdnf5::Base & base) {
    const auto cachedir = base.get_config().get_cachedir_option().get_value();
    if (!cachedir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(cachedir, ec);
        if (ec) {
            log_err("Unable to clean cache " + cachedir + ": " + ec.message());
            return 1;
        }
    }
    log_ok("Cache cleaned");
    return 0;
}

}  // namespace

int main(int argc, char ** argv) {
    banner();

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    if (argc >= 2) {
        const std::string first{argv[1]};
        if (first == "--help" || first == "-h") {
            usage(argv[0]);
            return 0;
        }
        if (first == "--version") {
            std::cout << VERSION << "\n";
            return 0;
        }
    }

    try {
        const auto opts = parse_options(argc, argv);

        if (is_transaction_command(opts.command) && geteuid() != 0) {
            log_err("Package transactions require root.");
            return 1;
        }

        libdnf5::Base base;

        if (opts.command == "clean") {
            prepare_base(base, opts.source, false, false);
            return command_clean(base);
        }

        if (opts.command == "repos") {
            prepare_base(base, opts.source, false, false);
            return command_repos(base);
        }

        const bool write_lock = is_transaction_command(opts.command);
        prepare_base(base, opts.source, write_lock, true);

        if (opts.command == "install") {
            return command_install(base, opts);
        }
        if (opts.command == "remove") {
            return command_remove(base, opts);
        }
        if (opts.command == "update" || opts.command == "upgrade") {
            return command_upgrade(base, opts);
        }
        if (opts.command == "search") {
            return command_search(base, opts);
        }
        if (opts.command == "info") {
            return command_info(base, opts);
        }
        if (opts.command == "list") {
            return command_list(base, opts);
        }

        usage(argv[0]);
        return 1;
    } catch (const std::exception & ex) {
        log_err(ex.what());
        return 1;
    }
}
