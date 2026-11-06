#include "cli.hpp"

#include "version.hpp"

#include <algorithm>
#include <charconv>
#include <locale>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>

namespace husk {
namespace {

template <typename T>
bool parse_number(std::string_view text, T &value) {
    static_assert(std::is_integral_v<T>);
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_int(std::string_view text, int &value) {
    return parse_number(text, value);
}

bool parse_int64(std::string_view text, std::int64_t &value) {
    return parse_number(text, value);
}

bool parse_uint64(std::string_view text, std::uint64_t &value) {
    return parse_number(text, value);
}

bool parse_double(std::string_view text, double &value) {
    if (text.empty()) {
        return false;
    }
    // libc++ in Xcode 16 does not implement floating-point from_chars.
    std::istringstream stream{std::string(text)};
    stream.imbue(std::locale::classic());
    stream >> std::noskipws >> value;
    return stream && stream.eof();
}

ParseResult fail(std::string message) {
    ParseResult result;
    result.error = std::move(message);
    return result;
}

bool set_flag(std::string_view name, Options &options, bool &handled) {
    handled = true;
    if (name == "--drop-seq") {
        options.drop_seq = true;
    } else if (name == "--keep-qual") {
        options.keep_qual = true;
    } else if (name == "--keep-name") {
        options.keep_name = true;
    } else if (name == "--drop-rg") {
        options.drop_rg = true;
    } else if (name == "--index") {
        options.index = true;
    } else if (name == "--force") {
        options.force = true;
    } else if (name == "--verify") {
        options.verify = true;
    } else {
        handled = false;
    }
    return handled;
}

ParseResult parse_long_option(
    const std::string &argument,
    const std::vector<std::string> &args,
    std::size_t &index,
    Options &options
) {
    const std::size_t equals = argument.find('=');
    const std::string name = argument.substr(0, equals);
    const bool has_value = equals != std::string::npos;
    const std::string value =
        has_value ? argument.substr(equals + 1) : std::string{};

    if (name == "--help") {
        if (has_value) {
            return fail("option '--help' does not take a value");
        }
        ParseResult result;
        result.action = Action::Help;
        return result;
    }
    if (name == "--version") {
        if (has_value) {
            return fail("option '--version' does not take a value");
        }
        ParseResult result;
        result.action = Action::Version;
        return result;
    }

    bool handled = false;
    set_flag(name, options, handled);
    if (handled) {
        if (has_value) {
            return fail("option '" + name + "' does not take a value");
        }
        ParseResult result;
        result.options = options;
        return result;
    }

    const bool is_threads = name == "--threads";
    const bool is_compression = name == "--compression";
    const bool is_downsample_fraction = name == "--downsample-fraction";
    const bool is_downsample_count = name == "--downsample-count";
    const bool is_downsample_mode = name == "--downsample-mode";
    const bool is_downsample_seed = name == "--downsample-seed";
    if (!is_threads && !is_compression && !is_downsample_fraction &&
        !is_downsample_count && !is_downsample_mode && !is_downsample_seed) {
        return fail("unrecognized option '" + name + "'");
    }

    std::string option_value = value;
    if (!has_value) {
        if (index + 1 >= args.size()) {
            return fail("option '" + name + "' requires a value");
        }
        option_value = args[++index];
    }
    if (option_value.empty()) {
        return fail("option '" + name + "' requires a value");
    }

    if (is_downsample_mode) {
        if (option_value == "total") {
            options.downsample.mode = DownsampleMode::Total;
        } else if (
            option_value == "sample" || option_value == "sample-wise" ||
            option_value == "samplewise"
        ) {
            options.downsample.mode = DownsampleMode::Sample;
        } else {
            return fail(
                "--downsample-mode must be 'sample' or 'total', got '" +
                option_value + "'"
            );
        }
        options.downsample_mode_set = true;
        ParseResult result;
        result.options = options;
        return result;
    }

    if (is_downsample_fraction) {
        double parsed = 0.0;
        if (!parse_double(option_value, parsed)) {
            return fail(
                "option '" + name + "' expects a number, got '" +
                option_value + "'"
            );
        }
        if (!(parsed > 0.0 && parsed <= 1.0)) {
            return fail("--downsample-fraction must be greater than 0 and at most 1");
        }
        options.downsample.fraction = parsed;
        ParseResult result;
        result.options = options;
        return result;
    }

    if (is_downsample_count) {
        std::int64_t parsed = 0;
        if (!parse_int64(option_value, parsed)) {
            return fail(
                "option '" + name + "' expects an integer, got '" +
                option_value + "'"
            );
        }
        if (parsed < 1) {
            return fail("--downsample-count must be at least 1");
        }
        options.downsample.count = parsed;
        ParseResult result;
        result.options = options;
        return result;
    }

    if (is_downsample_seed) {
        std::uint64_t parsed = 0;
        if (!parse_uint64(option_value, parsed)) {
            return fail(
                "option '" + name + "' expects a non-negative integer, got '" +
                option_value + "'"
            );
        }
        options.downsample.seed = parsed;
        options.downsample_seed_set = true;
        ParseResult result;
        result.options = options;
        return result;
    }

    int parsed = 0;
    if (!parse_int(option_value, parsed)) {
        return fail(
            "option '" + name + "' expects an integer, got '" + option_value +
            "'"
        );
    }
    if (is_threads) {
        if (parsed < 1) {
            return fail("--threads must be at least 1");
        }
        options.threads = parsed;
    } else {
        if (parsed < 0 || parsed > 9) {
            return fail("--compression must be between 0 and 9");
        }
        options.compression = parsed;
    }

    ParseResult result;
    result.options = options;
    return result;
}

}  // namespace

int default_threads() {
    unsigned int available = std::thread::hardware_concurrency();
    if (available == 0) {
        available = 1;
    }
    return static_cast<int>(std::min(4u, available));
}

ParseResult parse_args(const std::vector<std::string> &args) {
    Options options;
    std::vector<std::string> positional;
    bool options_ended = false;

    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string &argument = args[index];

        if (!options_ended && argument == "--") {
            options_ended = true;
            continue;
        }

        if (!options_ended && argument.rfind("--", 0) == 0) {
            ParseResult result =
                parse_long_option(argument, args, index, options);
            if (result.action != Action::Run || !result.error.empty()) {
                return result;
            }
            continue;
        }

        if (!options_ended && argument.size() > 1 && argument[0] == '-') {
            if (argument == "-h") {
                ParseResult result;
                result.action = Action::Help;
                return result;
            }
            if (argument == "-@" || argument.rfind("-@", 0) == 0) {
                std::string value = argument.size() > 2
                                        ? argument.substr(2)
                                        : std::string{};
                if (value.empty()) {
                    if (index + 1 >= args.size()) {
                        return fail("option '-@' requires a value");
                    }
                    value = args[++index];
                }
                int parsed = 0;
                if (!parse_int(value, parsed) || parsed < 1) {
                    return fail("--threads must be at least 1");
                }
                options.threads = parsed;
                continue;
            }
            return fail("unrecognized option '" + argument + "'");
        }

        positional.push_back(argument);
    }

    if (positional.size() != 2) {
        return fail("expected <in.bam> and <out.bam>");
    }
    if (options.drop_seq && options.keep_qual) {
        return fail("--keep-qual requires SEQ; drop --drop-seq");
    }
    if (options.downsample.fraction.has_value() &&
        options.downsample.count.has_value()) {
        return fail(
            "use only one of --downsample-fraction and --downsample-count"
        );
    }
    if (!options.downsample.enabled() && options.downsample_mode_set) {
        return fail(
            "--downsample-mode requires --downsample-fraction or "
            "--downsample-count"
        );
    }
    if (!options.downsample.enabled() && options.downsample_seed_set) {
        return fail(
            "--downsample-seed requires --downsample-fraction or "
            "--downsample-count"
        );
    }

    options.input = positional[0];
    options.output = positional[1];

    ParseResult result;
    result.options = options;
    return result;
}

ParseResult parse_args(int argc, char **argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    return parse_args(args);
}

void print_usage(std::FILE *out) {
    std::fprintf(
        out,
        "Usage: husk [options] <in.bam> <out.bam>\n"
        "\n"
        "Options:\n"
        "  --drop-seq          Remove SEQ (read length comes from CIGAR)\n"
        "  --keep-qual         Keep QUAL (requires SEQ)\n"
        "  --keep-name         Keep read names\n"
        "  --drop-rg           Drop RG tags (single-sample only)\n"
        "  -@, --threads N     htslib I/O threads (default: min(4, cores))\n"
        "  --index             Write out.bam.csi with min_shift=14\n"
        "  --compression N     BGZF compression level 0-9 (default 6)\n"
        "  --downsample-fraction F\n"
        "                      Keep a random fraction F of reads (0 < F <= 1)\n"
        "  --downsample-count N\n"
        "                      Keep N random reads per downsampling group\n"
        "  --downsample-mode MODE\n"
        "                      sample or total (default: total)\n"
        "  --downsample-seed N\n"
        "                      Random seed (default: 0)\n"
        "  --force             Overwrite an existing output\n"
        "  --verify            Reopen and validate the output after writing\n"
        "  --version           Print version and git SHA\n"
        "  --help              Print usage\n"
        "\n"
        "By default QUAL, read names and all optional tags except RG are\n"
        "removed; SEQ is kept. @PG and @CO header lines are always removed.\n"
        "Downsampling is record-level. In sample mode each @RG SM value is a\n"
        "group; reads without a known sample form one unknown group.\n"
    );
}

void print_version(std::FILE *out) {
    std::fprintf(out, "husk %s (%s)\n", HUSK_VERSION, HUSK_GIT_SHA);
}

}  // namespace husk
