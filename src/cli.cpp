#include "cli.hpp"

#include "version.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <thread>

namespace husk {
namespace {

bool parse_int(std::string_view text, int &value) {
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
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
    if (!is_threads && !is_compression) {
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

    int parsed = 0;
    if (!parse_int(option_value, parsed)) {
        return fail("option '" + name + "' expects an integer, got '" +
                    option_value + "'");
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
        "  --force             Overwrite an existing output\n"
        "  --verify            Reopen and validate the output after writing\n"
        "  --version           Print version and git SHA\n"
        "  --help              Print usage\n"
        "\n"
        "By default QUAL, read names and all optional tags except RG are\n"
        "removed; SEQ is kept. @PG and @CO header lines are always removed.\n"
    );
}

void print_version(std::FILE *out) {
    std::fprintf(out, "husk %s (%s)\n", HUSK_VERSION, HUSK_GIT_SHA);
}

}  // namespace husk
