#include "cli.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::string input(reinterpret_cast<const char *>(data), size);
    std::vector<std::string> args;

    std::size_t start = 0;
    while (start <= input.size() && args.size() < 32) {
        const std::size_t end = input.find('\0', start);
        std::string argument =
            input.substr(start, end == std::string::npos ? end : end - start);
        if (argument.size() > 256) {
            argument.resize(256);
        }
        args.push_back(std::move(argument));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    (void)husk::parse_args(args);
    return 0;
}
