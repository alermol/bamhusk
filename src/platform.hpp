#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace husk {

std::int64_t file_size(const std::filesystem::path &path);
int process_id();
bool same_file(const std::filesystem::path &a, const std::filesystem::path &b);
std::filesystem::path temporary_path_for(const std::filesystem::path &output);
bool replace_file(
    const std::filesystem::path &from,
    const std::filesystem::path &to,
    std::string &error
);
bool remove_file(const std::filesystem::path &path) noexcept;
void install_signal_handlers();
bool interrupted();

}  // namespace husk
