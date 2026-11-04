#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace husk {

inline constexpr int kDefaultCompression = 6;

int default_threads();

struct Options {
    bool drop_seq = false;
    bool keep_qual = false;
    bool keep_name = false;
    bool drop_rg = false;
    bool index = false;
    bool force = false;
    bool verify = false;
    int threads = default_threads();
    int compression = kDefaultCompression;
    std::filesystem::path input;
    std::filesystem::path output;
};

enum class Action : std::uint8_t {
    Run,
    Help,
    Version,
};

struct ParseResult {
    Action action = Action::Run;
    Options options;
    std::string error;
};

ParseResult parse_args(const std::vector<std::string> &args);
ParseResult parse_args(int argc, char **argv);
void print_usage(std::FILE *out);
void print_version(std::FILE *out);

}  // namespace husk
