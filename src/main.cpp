#include "bam_slim.hpp"
#include "cli.hpp"
#include "platform.hpp"

#include <cstdio>

namespace {

void print_stats(const husk::SlimStats &stats) {
    if (stats.input_bytes > 0 && stats.output_bytes >= 0) {
        const double smaller =
            100.0 *
            static_cast<double>(stats.input_bytes - stats.output_bytes) /
            static_cast<double>(stats.input_bytes);
        std::fprintf(
            stderr,
            "husk: %lld reads; %lld -> %lld bytes (%.1f%% smaller)\n",
            static_cast<long long>(stats.reads),
            static_cast<long long>(stats.input_bytes),
            static_cast<long long>(stats.output_bytes),
            smaller
        );
    } else {
        std::fprintf(
            stderr,
            "husk: %lld reads\n",
            static_cast<long long>(stats.reads)
        );
    }
}

}  // namespace

int main(int argc, char **argv) {
    husk::install_signal_handlers();

    const husk::ParseResult parsed = husk::parse_args(argc, argv);
    if (!parsed.error.empty()) {
        std::fprintf(stderr, "husk: %s\n", parsed.error.c_str());
        husk::print_usage(stderr);
        return 2;
    }
    if (parsed.action == husk::Action::Help) {
        husk::print_usage(stdout);
        return 0;
    }
    if (parsed.action == husk::Action::Version) {
        husk::print_version(stdout);
        return 0;
    }

    const husk::Options &options = parsed.options;
    husk::OutputOptions output;
    output.force = options.force;
    output.input = options.input;
    output.output = options.output;

    husk::SlimOptions slim;
    slim.drop_seq = options.drop_seq;
    slim.keep_qual = options.keep_qual;
    slim.keep_name = options.keep_name;
    slim.drop_rg = options.drop_rg;
    slim.index = options.index;
    slim.verify = options.verify;
    slim.threads = options.threads;
    slim.compression = options.compression;

    husk::SlimStats stats;
    const int result = husk::write_slim_bam(output, slim, &stats);
    if (result == 0) {
        print_stats(stats);
    }
    return result;
}
