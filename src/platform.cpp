#include "platform.hpp"

#include <csignal>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace husk {
namespace {

volatile std::sig_atomic_t g_interrupted = 0;

void handle_signal(int) {
    g_interrupted = 1;
}

std::filesystem::path with_suffix(
    const std::filesystem::path &path,
    const std::string &suffix
) {
    std::filesystem::path result = path;
    result += suffix;
    return result;
}

}  // namespace

std::int64_t file_size(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return -1;
    }
    return static_cast<std::int64_t>(size);
}

int process_id() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

bool same_file(const std::filesystem::path &a, const std::filesystem::path &b) {
    std::error_code error;
    if (std::filesystem::exists(a, error) && std::filesystem::exists(b, error)) {
        error.clear();
        if (std::filesystem::equivalent(a, b, error) && !error) {
            return true;
        }
    }

    error.clear();
    const auto canonical_a = std::filesystem::weakly_canonical(a, error);
    if (error) {
        return false;
    }
    error.clear();
    const auto canonical_b = std::filesystem::weakly_canonical(b, error);
    if (error) {
        return false;
    }
    return canonical_a == canonical_b;
}

enum class PathState : std::uint8_t {
    Available,
    Occupied,
    Error,
};

PathState path_state(const std::filesystem::path &path) {
    const auto state_of = [](const std::filesystem::path &candidate) {
        std::error_code error;
        const auto status =
            std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory) {
            return PathState::Available;
        }
        if (error) {
            return PathState::Error;
        }
        // exists(status) is true for symlinks, including dangling ones.
        return std::filesystem::exists(status) ? PathState::Occupied
                                               : PathState::Available;
    };

    const PathState path_status = state_of(path);
    if (path_status != PathState::Available) {
        return path_status;
    }

    // The caller may create an adjacent .csi index for this path. Reserve
    // both names so an existing file or symlink is never followed.
    return state_of(with_suffix(path, ".csi"));
}

std::filesystem::path temporary_path_for(const std::filesystem::path &output) {
    const std::string base = ".tmp." + std::to_string(process_id());
    std::filesystem::path candidate = with_suffix(output, base);
    for (int attempt = 1;; ++attempt) {
        const PathState state = path_state(candidate);
        if (state == PathState::Available) {
            return candidate;
        }
        if (state == PathState::Error) {
            return {};
        }
        candidate = with_suffix(output, base + "." + std::to_string(attempt));
    }
}

bool replace_file(
    const std::filesystem::path &from,
    const std::filesystem::path &to,
    std::string &error
) {
#ifdef _WIN32
    if (MoveFileExW(
            from.wstring().c_str(),
            to.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) == 0) {
        error = "cannot replace '" + to.string() + "': " +
                std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
#else
    if (::rename(from.c_str(), to.c_str()) != 0) {
        error = "cannot replace '" + to.string() + "': " +
                std::generic_category().message(errno);
        return false;
    }
    return true;
#endif
}

bool remove_file(const std::filesystem::path &path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
    return !error;
}

void install_signal_handlers() {
    g_interrupted = 0;
    std::signal(SIGINT, handle_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_signal);
#endif
#ifdef SIGBREAK
    std::signal(SIGBREAK, handle_signal);
#endif
}

bool interrupted() {
    return g_interrupted != 0;
}

}  // namespace husk
