#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/common/sack/query_cmp.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/transaction/transaction_item_action.hpp>
#include <libdnf5/utils/locker.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

constexpr const char * VERSION = "0.2.0";

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
    std::cerr << "SystemPackager 2 by Adava Software for Linux in 2026 v" << VERSION << "\n";
}

void usage(const char * prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " <package>... [--source auto|adava|fedora] [-y]\n"
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
        throw std::runtime_error("No command or package specified");
    }

    if (is_command(positional.front())) {
        opts.command = positional.front();
        opts.args.assign(positional.begin() + 1, positional.end());
    } else {
        opts.command = "install";
        opts.args = std::move(positional);
    }

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

void prepare_base(libdnf5::Base & base, SourceMode source, bool write_lock, bool load_repositories = true) {
    auto & config = base.get_config();
    config.get_plugins_option().set(false);
    config.get_installroot_option().set("/");

    // Loading dnf.conf is optional. AdavaLinux intentionally keeps the package
    // policy in the vendor .repo files and libdnf5 defaults.
    base.setup();

    auto repo_sack = base.get_repo_sack();
    repo_sack->create_repos_from_system_configuration();
    apply_repo_selection(base, source);

    if (!load_repositories) {
        return;
    }

    base.lock_system_repo(
        write_lock ? libdnf5::utils::LockAccess::WRITE : libdnf5::utils::LockAccess::READ,
        libdnf5::utils::LockBlocking::BLOCKING);
    repo_sack->load_repos();
}

bool confirm_transaction(bool assume_yes) {
    if (assume_yes || !isatty(STDIN_FILENO)) {
        return true;
    }

    std::cerr << "Continue? [Y/n] ";
    std::string answer;
    std::getline(std::cin, answer);
    return answer.empty() || answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

void print_transaction(libdnf5::base::Transaction & transaction) {
    const auto packages = transaction.get_transaction_packages();
    if (packages.empty()) {
        return;
    }

    std::cout << "\nTransaction:\n";
    for (const auto & item : packages) {
        std::cout << "  "
                  << libdnf5::transaction::transaction_item_action_to_string(item.get_action())
                  << "  " << item.get_package().get_nevra()
                  << "  [" << item.get_package().get_repo_id() << "]\n";
    }
    std::cout << "\n";
}

int run_goal(libdnf5::Goal & goal, bool assume_yes, const std::string & description) {
    auto transaction = goal.resolve();

    for (const auto & line : transaction.get_resolve_logs_as_strings()) {
        std::cerr << line << "\n";
    }

    if (fatal_resolve_problem(transaction.get_problems())) {
        std::cerr << "ERR: Dependency resolution failed.\n";
        return 2;
    }

    if (transaction.empty()) {
        std::cout << "Nothing to do.\n";
        return 0;
    }

    print_transaction(transaction);
    if (!confirm_transaction(assume_yes)) {
        std::cout << "Cancelled.\n";
        return 0;
    }

    transaction.set_description(description);

    try {
        std::cout << "Downloading packages...\n";
        transaction.download();
        std::cout << "Running RPM transaction...\n";
        const auto result = transaction.run();
        if (result != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            std::cerr << "ERR: "
                      << libdnf5::base::Transaction::transaction_result_to_string(result)
                      << "\n";
            for (const auto & problem : transaction.get_transaction_problems()) {
                std::cerr << "  " << problem << "\n";
            }
            return 3;
        }
    } catch (const std::exception & ex) {
        std::cerr << "ERR: " << ex.what() << "\n";
        return 3;
    }

    std::cout << "Done.\n";
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
    std::cout << pkg.get_name() << "." << pkg.get_arch()
              << "\t" << pkg.get_version() << "-" << pkg.get_release()
              << "\t" << pkg.get_repo_id();
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
        std::cout << "No matching packages.\n";
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
            std::cout << "Name:        " << pkg.get_name() << "\n"
                      << "Version:     " << pkg.get_version() << "-" << pkg.get_release() << "\n"
                      << "Architecture:" << " " << pkg.get_arch() << "\n"
                      << "Repository:  " << pkg.get_repo_id() << "\n"
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
        std::cout << repo->get_id() << "\t"
                  << (repo->is_enabled() ? "enabled" : "disabled") << "\t"
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
            std::cerr << "ERR: Unable to clean cache " << cachedir << ": " << ec.message() << "\n";
            return 1;
        }
    }
    std::cout << "Cache cleaned.\n";
    return 0;
}

}  // namespace

int main(int argc, char ** argv) {
    banner();

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
            std::cerr << "ERR: Package transactions require root.\n";
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
        std::cerr << "ERR: " << ex.what() << "\n";
        return 1;
    }
}
