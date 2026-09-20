#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/common/sack/query_cmp.hpp>
#include <libdnf5/logger/logger.hpp>
#include <libdnf5/repo/download_callbacks.hpp>
#include <libdnf5/repo/package_downloader.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/transaction/transaction_item_action.hpp>
#include <libdnf5/utils/locker.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
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

constexpr int NETWORK_TIMEOUT_SECONDS = 5;
constexpr int MAX_MIRROR_TRIES = 3;
constexpr std::uint32_t MAX_PARALLEL_DOWNLOADS = 8;
constexpr std::uint32_t MAX_DOWNLOADS_PER_MIRROR = 4;

std::mutex console_mutex;
bool status_line_active = false;

bool interactive_terminal() {
    return isatty(STDOUT_FILENO) != 0;
}

void clear_status_line_locked() {
    if (!status_line_active || !interactive_terminal()) {
        return;
    }
    std::cout << "\r\033[2K" << std::flush;
    status_line_active = false;
}

void render_status_line(const std::string & message) {
    if (!interactive_terminal()) {
        return;
    }

    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\r\033[2K" << message << std::flush;
    status_line_active = true;
}

void log_line(std::ostream & stream, const char * color, const char * prefix, const std::string & message) {
    std::lock_guard<std::mutex> lock(console_mutex);
    clear_status_line_locked();
    stream << color << prefix << COLOR_RESET << message << "\n" << std::flush;
}

void log_err(const std::string & message) {
    log_line(std::cerr, COLOR_RED, "ERR: ", message);
}

void log_warn(const std::string & message) {
    log_line(std::cerr, COLOR_YELLOW, "WARN: ", message);
}

void log_ok(const std::string & message) {
    log_line(std::cout, COLOR_GREEN, "OK: ", message);
}

void log_info(const std::string & message) {
    log_line(std::cout, COLOR_CYAN, "INFO: ", message);
}

std::string elapsed_string(std::chrono::steady_clock::duration elapsed) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::ostringstream out;
    if (milliseconds < 1000) {
        out << milliseconds << " ms";
    } else {
        out << std::fixed << std::setprecision(1) << (milliseconds / 1000.0) << " s";
    }
    return out.str();
}

class ActivitySpinner {
public:
    explicit ActivitySpinner(std::string message) : message_(std::move(message)) {
        if (!interactive_terminal()) {
            log_info(message_ + "...");
            return;
        }

        running_.store(true);
        worker_ = std::thread([this]() {
            static constexpr char frames[] = {'|', '/', '-', '\\'};
            std::size_t frame = 0;
            while (running_.load()) {
                render_status_line(
                    std::string(COLOR_CYAN) + frames[frame++ % 4] + COLOR_RESET + " " + message_ + "...");
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
            }
        });
    }

    ActivitySpinner(const ActivitySpinner &) = delete;
    ActivitySpinner & operator=(const ActivitySpinner &) = delete;

    ~ActivitySpinner() {
        stop();
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }

        std::lock_guard<std::mutex> lock(console_mutex);
        clear_status_line_locked();
    }

private:
    std::string message_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

std::string make_percent_bar(int percent, int width = 28) {
    percent = std::clamp(percent, 0, 100);
    const int filled = (percent * width) / 100;

    std::string bar;
    bar.reserve(static_cast<std::size_t>(width));
    for (int i = 0; i < width; ++i) {
        bar += i < filled ? '#' : '-';
    }
    return bar;
}

class MetadataActivity {
public:
    MetadataActivity(
        const std::vector<std::string> & repositories,
        std::unordered_map<std::string, std::vector<std::string>> plans)
        : repositories_(repositories), plans_(std::move(plans)), total_(repositories.size()) {
        for (const auto & repo_id : repositories_) {
            repo_progress_.emplace(repo_id, RepoProgress{});
        }

        recalculate_processing_total();

        if (!interactive_terminal()) {
            log_info("Repository metadata: 0/" + std::to_string(total_) + " (0%)");
            return;
        }

        running_.store(true);
        worker_ = std::thread([this]() {
            static constexpr char frames[] = {'|', '/', '-', '\\'};
            std::size_t frame = 0;

            while (running_.load()) {
                std::vector<std::string> details;
                double aggregate_total = 0.0;
                double aggregate_downloaded = 0.0;
                bool have_byte_progress = false;
                std::size_t processed_items = 0;
                std::size_t processing_total = 0;
                bool processing = false;
                std::string current_item;

                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    processing = processing_started_;
                    processed_items = processed_items_.size();
                    processing_total = processing_total_;
                    current_item = current_processing_item_;

                    for (const auto & repo_id : repositories_) {
                        const auto it = repo_progress_.find(repo_id);
                        if (it == repo_progress_.end()) {
                            continue;
                        }

                        const auto & state = it->second;
                        if (!processing && state.total > 0.0) {
                            have_byte_progress = true;
                            aggregate_total += state.total;
                            aggregate_downloaded += std::min(state.downloaded, state.total);

                            const int repo_percent = std::clamp(
                                static_cast<int>((state.downloaded * 100.0) / state.total), 0, 100);
                            details.push_back(repo_id + " " + std::to_string(repo_percent) + "%");
                        } else if (!processing && state.failed) {
                            details.push_back(repo_id + " FAILED");
                        } else if (!processing && state.finished) {
                            details.push_back(repo_id + " 100%");
                        } else if (!processing) {
                            details.push_back(repo_id + " waiting");
                        } else {
                            const auto plan_it = plans_.find(repo_id);
                            const std::size_t repo_total =
                                plan_it == plans_.end() ? 0 : plan_it->second.size();
                            const auto done_it = processed_per_repo_.find(repo_id);
                            const std::size_t repo_done =
                                done_it == processed_per_repo_.end() ? 0 : done_it->second;
                            details.push_back(
                                repo_id + " " + std::to_string(repo_done) + "/" +
                                std::to_string(repo_total));
                        }
                    }
                }

                const auto done = completed_.load();
                int percent = 0;
                std::string phase;

                if (processing) {
                    percent = processing_total == 0
                        ? 100
                        : std::clamp(
                              static_cast<int>((processed_items * 100U) / processing_total), 0, 100);
                    phase = "Processing metadata";
                    if (!current_item.empty()) {
                        phase += ": " + current_item;
                    }
                } else {
                    if (have_byte_progress && aggregate_total > 0.0) {
                        percent = std::clamp(
                            static_cast<int>((aggregate_downloaded * 100.0) / aggregate_total), 0, 100);
                    } else if (total_ > 0) {
                        percent = static_cast<int>((done * 100U) / total_);
                    } else {
                        percent = 100;
                    }
                    phase = "Repository metadata";
                }

                std::ostringstream out;
                out << COLOR_CYAN << frames[frame++ % 4] << COLOR_RESET << " "
                    << phase << " "
                    << COLOR_GREEN << "[" << make_percent_bar(percent) << "]" << COLOR_RESET << " "
                    << std::setw(3) << percent << "%";

                if (processing) {
                    out << "  " << processed_items << "/" << processing_total;
                } else {
                    out << "  " << done << "/" << total_;
                }

                if (!details.empty()) {
                    out << "  {";
                    for (std::size_t i = 0; i < details.size(); ++i) {
                        if (i != 0) {
                            out << ", ";
                        }
                        out << details[i];
                    }
                    out << "}";
                }

                render_status_line(out.str());
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
            }
        });
    }

    MetadataActivity(const MetadataActivity &) = delete;
    MetadataActivity & operator=(const MetadataActivity &) = delete;

    ~MetadataActivity() {
        stop();
    }

    void update_repo_progress(const std::string & repo_id, double total, double downloaded) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto & state = repo_progress_[repo_id];
        if (total > 0.0) {
            state.total = total;
        }
        if (downloaded >= 0.0) {
            state.downloaded = downloaded;
        }
    }

    void repository_finished(const std::string & repo_id, bool success) {
        bool count_it = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto & state = repo_progress_[repo_id];
            if (!state.finished) {
                state.finished = true;
                state.failed = !success;
                if (success && state.total > 0.0) {
                    state.downloaded = state.total;
                }
                count_it = true;
            }
        }

        if (!count_it) {
            return;
        }

        const auto done = ++completed_;
        if (!interactive_terminal()) {
            log_info(
                "Repository metadata: " + std::to_string(done) + "/" +
                std::to_string(total_) + " repositories complete");
        }
    }

    void processing_item(const std::string & repo_id, const std::string & item) {
        std::size_t index = 0;
        std::size_t repo_total = 0;
        bool newly_processed = false;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            processing_started_ = true;

            auto & plan = plans_[repo_id];
            auto it = std::find(plan.begin(), plan.end(), item);
            if (it == plan.end()) {
                plan.push_back(item);
                recalculate_processing_total_locked();
                it = std::prev(plan.end());
            }

            index = static_cast<std::size_t>(std::distance(plan.begin(), it)) + 1;
            repo_total = plan.size();
            current_processing_item_ = repo_id + "/" + item;

            const std::string key = repo_id + "\n" + item;
            if (processed_items_.insert(key).second) {
                ++processed_per_repo_[repo_id];
                newly_processed = true;
            }
        }

        if (newly_processed) {
            log_info(
                "[" + repo_id + "] processing metadata " +
                std::to_string(index) + "/" + std::to_string(repo_total) +
                ": " + item);
        }
    }

    void finish_processing() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        processing_started_ = true;
        for (const auto & [repo_id, items] : plans_) {
            for (const auto & item : items) {
                processed_items_.insert(repo_id + "\n" + item);
            }
            processed_per_repo_[repo_id] = items.size();
        }
        current_processing_item_.clear();
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }

        std::lock_guard<std::mutex> lock(console_mutex);
        clear_status_line_locked();
    }

private:
    struct RepoProgress {
        double total{0.0};
        double downloaded{0.0};
        bool finished{false};
        bool failed{false};
    };

    void recalculate_processing_total() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        recalculate_processing_total_locked();
    }

    void recalculate_processing_total_locked() {
        processing_total_ = 0;
        for (const auto & [repo_id, items] : plans_) {
            (void)repo_id;
            processing_total_ += items.size();
        }
    }

    std::vector<std::string> repositories_;
    std::unordered_map<std::string, std::vector<std::string>> plans_;
    const std::size_t total_;
    std::atomic<std::size_t> completed_{0};
    std::atomic<bool> running_{false};

    std::mutex state_mutex_;
    std::unordered_map<std::string, RepoProgress> repo_progress_;
    bool processing_started_{false};
    std::size_t processing_total_{0};
    std::set<std::string> processed_items_;
    std::unordered_map<std::string, std::size_t> processed_per_repo_;
    std::string current_processing_item_;

    std::thread worker_;
};

class SyspckgLogger : public libdnf5::Logger {
public:
    void set_metadata_activity(MetadataActivity * activity) noexcept {
        metadata_activity_.store(activity);
    }

    void write(
        [[maybe_unused]] const std::chrono::time_point<std::chrono::system_clock> & time,
        [[maybe_unused]] pid_t pid,
        [[maybe_unused]] Level level,
        const std::string & message) noexcept override {
        auto * activity = metadata_activity_.load();
        if (!activity) {
            return;
        }

        try {
            constexpr std::string_view main_prefix = "Loading repomd and primary for repo \"";
            if (message.starts_with(main_prefix)) {
                const auto repo_start = main_prefix.size();
                const auto repo_end = message.find('"', repo_start);
                if (repo_end != std::string::npos) {
                    const auto repo_id = message.substr(repo_start, repo_end - repo_start);
                    activity->processing_item(repo_id, "repomd.xml");
                    activity->processing_item(repo_id, "primary");
                }
                return;
            }

            constexpr std::string_view ext_prefix = "Loading ";
            constexpr std::string_view ext_marker = " extension for repo \"";
            if (message.starts_with(ext_prefix)) {
                const auto marker_pos = message.find(ext_marker);
                if (marker_pos != std::string::npos) {
                    const auto item =
                        message.substr(ext_prefix.size(), marker_pos - ext_prefix.size());
                    const auto repo_start = marker_pos + ext_marker.size();
                    const auto repo_end = message.find('"', repo_start);
                    if (repo_end != std::string::npos) {
                        const auto repo_id = message.substr(repo_start, repo_end - repo_start);
                        activity->processing_item(repo_id, item);
                    }
                }
            }
        } catch (...) {
            // Logging must never break package management.
        }
    }

private:
    std::atomic<MetadataActivity *> metadata_activity_{nullptr};
};

SyspckgLogger * ui_logger = nullptr;

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

enum class DownloadContextKind {
    REPOSITORY,
    PACKAGE,
};

struct DownloadContext {
    explicit DownloadContext(DownloadContextKind kind) : kind(kind) {}
    DownloadContextKind kind;
};

struct RepoFetchContext : DownloadContext {
    RepoFetchContext(std::string repo_id, std::vector<std::string> metadata_items)
        : DownloadContext(DownloadContextKind::REPOSITORY),
          repo_id(std::move(repo_id)),
          metadata_items(std::move(metadata_items)) {}

    std::string repo_id;
    std::vector<std::string> metadata_items;
};

struct PackageFetchContext : DownloadContext {
    PackageFetchContext(std::string filename, std::size_t index, std::size_t total)
        : DownloadContext(DownloadContextKind::PACKAGE),
          filename(std::move(filename)),
          index(index),
          total(total) {}

    std::string filename;
    std::size_t index;
    std::size_t total;
};

class VerboseDownloadCallbacks : public libdnf5::repo::DownloadCallbacks {
public:
    explicit VerboseDownloadCallbacks(MetadataActivity * metadata_activity = nullptr)
        : metadata_activity_(metadata_activity) {}

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
        std::string result_key;
        bool repository_metadata{false};
        std::vector<std::string> metadata_items;
        std::size_t archive_index{0};
        std::size_t archive_total{0};
        int attempt{1};
        double total{0.0};
        int last_bucket{-10};
        std::size_t spinner_frame{0};
    };

    std::string progress_bar(const DownloadState & state, double downloaded, double total_to_download) const {
        int percent = total_to_download > 0.0
            ? static_cast<int>((downloaded * 100.0) / total_to_download)
            : 0;
        percent = std::clamp(percent, 0, 100);

        static constexpr char frames[] = {'|', '/', '-', '\\'};
        const char spinner = frames[state.spinner_frame % 4];

        std::ostringstream out;
        out << COLOR_CYAN << spinner << COLOR_RESET << " ";
        if (state.archive_total > 0) {
            out << "archive " << state.archive_index << "/" << state.archive_total << "  ";
        }
        out << state.description << " "
            << COLOR_GREEN << "[" << make_percent_bar(percent) << "]" << COLOR_RESET << " "
            << std::setw(3) << percent << "%";

        if (total_to_download > 0.0) {
            out << "  " << format_bytes(downloaded) << "/" << format_bytes(total_to_download);
        }
        if (state.archive_total > 0) {
            out << "  attempt " << state.attempt << "/" << MAX_MIRROR_TRIES;
        }

        return out.str();
    }

    void * add_new_download(
        [[maybe_unused]] void * user_data,
        const char * description,
        double total_to_download) override {
        const auto * context = static_cast<const DownloadContext *>(user_data);
        const auto * repo_context =
            context && context->kind == DownloadContextKind::REPOSITORY
                ? static_cast<const RepoFetchContext *>(context)
                : nullptr;
        const auto * package_context =
            context && context->kind == DownloadContextKind::PACKAGE
                ? static_cast<const PackageFetchContext *>(context)
                : nullptr;

        std::string label = description ? description : "(unknown download)";
        if (package_context && !package_context->filename.empty()) {
            label = package_context->filename;
        }

        downloads_.push_back(DownloadState{
            label,
            repo_context ? repo_context->repo_id : label,
            repo_context != nullptr,
            repo_context ? repo_context->metadata_items : std::vector<std::string>{},
            package_context ? package_context->index : 0,
            package_context ? package_context->total : 0,
            1,
            total_to_download,
            -10,
            0,
        });
        auto & state = downloads_.back();

        if (state.repository_metadata) {
            for (std::size_t i = 0; i < state.metadata_items.size(); ++i) {
                log_info(
                    "[" + state.result_key + "] metadata " +
                    std::to_string(i + 1) + "/" + std::to_string(state.metadata_items.size()) +
                    " queued: " + state.metadata_items[i]);
            }
        } else if (interactive_terminal()) {
            render_status_line(progress_bar(state, 0.0, total_to_download));
        } else {
            std::string prefix;
            if (state.archive_total > 0) {
                prefix =
                    "archive " + std::to_string(state.archive_index) + "/" +
                    std::to_string(state.archive_total) + " ";
            }
            log_info(
                "Fetch start: " + prefix + state.description +
                " (" + format_bytes(total_to_download) + "), attempt " +
                std::to_string(state.attempt) + "/" + std::to_string(MAX_MIRROR_TRIES));
        }
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
        ++state->spinner_frame;

        if (state->repository_metadata) {
            if (metadata_activity_) {
                metadata_activity_->update_repo_progress(
                    state->result_key, total_to_download, downloaded);
            }
            return OK;
        }

        if (interactive_terminal()) {
            render_status_line(progress_bar(*state, downloaded, total_to_download));
        } else if (bucket >= state->last_bucket + 10) {
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
        const std::string result_key = state ? state->result_key : description;
        auto & result = results_[result_key];

        if (state && !state->repository_metadata && interactive_terminal() && status != TransferStatus::ERROR) {
            render_status_line(progress_bar(*state, state->total, state->total));
        }

        switch (status) {
            case TransferStatus::SUCCESSFUL:
                ++result.successful;
                if (state && state->repository_metadata) {
                    for (std::size_t i = 0; i < state->metadata_items.size(); ++i) {
                        log_ok(
                            "[" + result_key + "] metadata " +
                            std::to_string(i + 1) + "/" + std::to_string(state->metadata_items.size()) +
                            " downloaded: " + state->metadata_items[i]);
                    }
                    if (metadata_activity_) {
                        metadata_activity_->repository_finished(result_key, true);
                    }
                } else if (state && state->archive_total > 0) {
                    log_ok(
                        "Archive " + std::to_string(state->archive_index) + "/" +
                        std::to_string(state->archive_total) +
                        " fetched: " + description);
                } else {
                    log_ok("Fetch complete: " + description);
                }
                break;
            case TransferStatus::ALREADYEXISTS:
                ++result.successful;
                if (state && state->repository_metadata) {
                    for (std::size_t i = 0; i < state->metadata_items.size(); ++i) {
                        log_info(
                            "[" + result_key + "] metadata " +
                            std::to_string(i + 1) + "/" + std::to_string(state->metadata_items.size()) +
                            " cache hit: " + state->metadata_items[i]);
                    }
                    if (metadata_activity_) {
                        metadata_activity_->repository_finished(result_key, true);
                    }
                } else {
                    log_info("Fetch cache hit: " + description);
                }
                break;
            case TransferStatus::ERROR: {
                ++result.failed;
                result.last_error = msg ? msg : "unknown download error";
                if (state && state->repository_metadata && metadata_activity_) {
                    metadata_activity_->repository_finished(result_key, false);
                }
                log_warn("Fetch failed: " + description + " -> " + result.last_error);

                if (
                    result.last_error.find("Failed writing") != std::string::npos ||
                    result.last_error.find("writing received data to disk") != std::string::npos ||
                    result.last_error.find("disk/application") != std::string::npos) {
                    log_warn(
                        "Download destination write failed; check free space and write access in "
                        "/var/cache/libdnf5 and the root filesystem");
                }
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
        auto * state = static_cast<DownloadState *>(user_cb_data);
        const int failed_attempt = state ? state->attempt : 1;

        std::string detail;
        if (state && state->archive_total > 0) {
            detail =
                "Archive " + std::to_string(state->archive_index) + "/" +
                std::to_string(state->archive_total) +
                " attempt " + std::to_string(failed_attempt) + "/" +
                std::to_string(MAX_MIRROR_TRIES) + " failed";
        } else {
            detail = "Mirror failed";
        }

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

        if (state && state->archive_total > 0 && state->attempt < MAX_MIRROR_TRIES) {
            ++state->attempt;
            if (interactive_terminal()) {
                render_status_line(progress_bar(*state, 0.0, state->total));
            } else {
                log_info(
                    "Retrying archive " + std::to_string(state->archive_index) + "/" +
                    std::to_string(state->archive_total) + ": " + state->description +
                    ", attempt " + std::to_string(state->attempt) + "/" +
                    std::to_string(MAX_MIRROR_TRIES));
            }
        }
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

    MetadataActivity * metadata_activity_{nullptr};
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
    if (access("/usr/bin/gpg", X_OK) != 0) {
        throw std::runtime_error("OpenPGP engine is missing: /usr/bin/gpg");
    }
    if (access("/usr/bin/gpgconf", X_OK) != 0) {
        throw std::runtime_error("OpenPGP configuration engine is missing: /usr/bin/gpgconf");
    }

    const int gpg_status = std::system("/usr/bin/gpg --version >/dev/null 2>&1");
    if (gpg_status != 0) {
        throw std::runtime_error(
            "OpenPGP engine exists but cannot start; rebuild the ISO with the private GnuPG runtime");
    }
}

void prepare_base(libdnf5::Base & base, SourceMode source, bool write_lock, bool load_repositories = true) {
    check_runtime_layout();

    auto & config = base.get_config();
    config.get_plugins_option().set(false);
    config.get_installroot_option().set("/");
    config.get_timeout_option().set(libdnf5::Option::Priority::RUNTIME, NETWORK_TIMEOUT_SECONDS);
    config.get_max_parallel_downloads_option().set(
        libdnf5::Option::Priority::RUNTIME, MAX_PARALLEL_DOWNLOADS);
    config.get_max_downloads_per_mirror_option().set(
        libdnf5::Option::Priority::RUNTIME, MAX_DOWNLOADS_PER_MIRROR);

    log_info(
        "Network policy: timeout " + std::to_string(NETWORK_TIMEOUT_SECONDS) +
        "s, max " + std::to_string(MAX_MIRROR_TRIES) +
        " mirror tries, " + std::to_string(MAX_PARALLEL_DOWNLOADS) +
        " parallel downloads");

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
    std::list<RepoFetchContext> repo_fetch_contexts;
    std::unordered_map<std::string, std::vector<std::string>> metadata_plans;
    {
        libdnf5::repo::RepoQuery repos(base);
        for (auto repo : repos) {
            if (repo->get_type() != libdnf5::repo::Repo::Type::AVAILABLE || !repo->is_enabled()) {
                continue;
            }

            const auto repo_id = repo->get_id();
            enabled_repo_ids.push_back(repo_id);

            std::vector<std::string> metadata_items{"repomd.xml", "primary"};
            const auto optional_metadata =
                config.get_optional_metadata_types_option().get_value();
            if (optional_metadata.contains("filelists")) {
                metadata_items.emplace_back("filelists");
            }
            if (optional_metadata.contains("other")) {
                metadata_items.emplace_back("other");
            }
            if (optional_metadata.contains("presto")) {
                metadata_items.emplace_back("presto");
            }
            if (optional_metadata.contains("updateinfo")) {
                metadata_items.emplace_back("updateinfo");
            }
            if (optional_metadata.contains("comps")) {
                metadata_items.emplace_back("comps");
            }

            metadata_plans.emplace(repo_id, metadata_items);
            repo_fetch_contexts.emplace_back(repo_id, std::move(metadata_items));
            repo->set_user_data(&repo_fetch_contexts.back());

            auto & repo_config = repo->get_config();
            repo_config.get_skip_if_unavailable_option().set(true);
            repo_config.get_timeout_option().set(
                libdnf5::Option::Priority::RUNTIME, NETWORK_TIMEOUT_SECONDS);
            repo->set_max_mirror_tries(MAX_MIRROR_TRIES);

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

    MetadataActivity metadata_activity(enabled_repo_ids, std::move(metadata_plans));
    if (ui_logger) {
        ui_logger->set_metadata_activity(&metadata_activity);
    }

    auto download_callbacks = std::make_unique<VerboseDownloadCallbacks>(&metadata_activity);
    auto * download_callbacks_ptr = download_callbacks.get();
    base.set_download_callbacks(std::move(download_callbacks));

    base.lock_system_repo(
        write_lock ? libdnf5::utils::LockAccess::WRITE : libdnf5::utils::LockAccess::READ,
        libdnf5::utils::LockBlocking::BLOCKING);

    const auto repo_load_started = std::chrono::steady_clock::now();
    repo_sack->load_repos();
    metadata_activity.finish_processing();
    if (ui_logger) {
        ui_logger->set_metadata_activity(nullptr);
    }
    metadata_activity.stop();
    log_ok(
        "Repository metadata processed in " +
        elapsed_string(std::chrono::steady_clock::now() - repo_load_started));

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

            for (const auto & context : repo_fetch_contexts) {
                if (context.repo_id != repo_id) {
                    continue;
                }
                for (std::size_t i = 0; i < context.metadata_items.size(); ++i) {
                    log_ok(
                        "[" + repo_id + "] metadata " +
                        std::to_string(i + 1) + "/" +
                        std::to_string(context.metadata_items.size()) +
                        " processed: " + context.metadata_items[i]);
                }
                break;
            }
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

int run_goal(libdnf5::Base & base, libdnf5::Goal & goal, bool assume_yes, const std::string & description) {
    const auto resolve_started = std::chrono::steady_clock::now();
    ActivitySpinner resolve_spinner("Resolving dependencies");
    auto transaction = goal.resolve();
    resolve_spinner.stop();
    log_ok(
        "Dependencies resolved in " +
        elapsed_string(std::chrono::steady_clock::now() - resolve_started));

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

        {
            std::error_code space_error;
            const auto cache_space = std::filesystem::space("/var/cache/libdnf5", space_error);
            if (!space_error) {
                log_info(
                    "Package cache free space: " +
                    format_bytes(static_cast<double>(cache_space.available)));
                if (cache_space.available < 64ULL * 1024ULL * 1024ULL) {
                    log_warn(
                        "Low package-cache free space; RPM downloads may fail with Curl error (23)");
                }
            } else {
                log_warn(
                    "Unable to determine free space for /var/cache/libdnf5: " +
                    space_error.message());
            }
        }

        const auto download_started = std::chrono::steady_clock::now();
        libdnf5::repo::PackageDownloader downloader(base);
        downloader.set_fail_fast(false);

        std::size_t archive_total = 0;
        for (auto & item : transaction.get_transaction_packages()) {
            if (!libdnf5::transaction::transaction_item_action_is_inbound(item.get_action())) {
                continue;
            }

            const auto & pkg = item.get_package();
            if (!transaction.get_download_local_pkgs() &&
                pkg.get_repo()->get_type() == libdnf5::repo::Repo::Type::COMMANDLINE) {
                continue;
            }
            ++archive_total;
        }

        log_info("Archives to download: " + std::to_string(archive_total));

        std::list<PackageFetchContext> package_contexts;
        std::size_t archive_index = 0;
        for (auto & item : transaction.get_transaction_packages()) {
            if (!libdnf5::transaction::transaction_item_action_is_inbound(item.get_action())) {
                continue;
            }

            const auto & pkg = item.get_package();
            if (!transaction.get_download_local_pkgs() &&
                pkg.get_repo()->get_type() == libdnf5::repo::Repo::Type::COMMANDLINE) {
                continue;
            }

            ++archive_index;
            std::string filename = std::filesystem::path(pkg.get_location()).filename().string();
            if (filename.empty()) {
                filename = pkg.get_full_nevra() + ".rpm";
            }

            package_contexts.emplace_back(filename, archive_index, archive_total);
            downloader.add(pkg, &package_contexts.back());
        }

        downloader.download();

        const auto failed_packages = downloader.get_failed_packages();
        if (!failed_packages.empty()) {
            for (const auto & pkg : failed_packages) {
                log_warn("Package fetch failed after retries: " + pkg.get_nevra());
            }
            log_err(
                "Unable to download " + std::to_string(failed_packages.size()) +
                " required package(s); RPM transaction will not be started");
            return 3;
        }

        log_ok(
            "Download complete in " +
            elapsed_string(std::chrono::steady_clock::now() - download_started));

        log_info("Preparing RPM transaction...");
        const auto transaction_started = std::chrono::steady_clock::now();
        ActivitySpinner transaction_spinner("Running RPM transaction");
        const auto result = transaction.run();
        transaction_spinner.stop();
        log_info(
            "RPM transaction finished in " +
            elapsed_string(std::chrono::steady_clock::now() - transaction_started));
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
    return run_goal(base, goal, opts.assume_yes, "syspckg2 install");
}

int command_remove(libdnf5::Base & base, const Options & opts) {
    if (opts.args.empty()) {
        throw std::runtime_error("remove requires at least one package");
    }

    libdnf5::Goal goal(base);
    for (const auto & spec : opts.args) {
        goal.add_rpm_remove(spec);
    }
    return run_goal(base, goal, opts.assume_yes, "syspckg2 remove");
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
    return run_goal(base, goal, opts.assume_yes, "syspckg2 upgrade");
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

        std::vector<std::unique_ptr<libdnf5::Logger>> loggers;
        auto logger = std::make_unique<SyspckgLogger>();
        ui_logger = logger.get();
        loggers.emplace_back(std::move(logger));
        libdnf5::Base base(std::move(loggers));

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
