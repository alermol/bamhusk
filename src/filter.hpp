#pragma once

#include <cstdint>
#include <string>

namespace husk {

struct FilterOptions {
    int require_flags = 0;
    int exclude_flags = 0;
    int min_mapq = 0;
    std::int64_t min_read_length = 0;
    std::string expression;

    bool enabled() const {
        return require_flags != 0 || exclude_flags != 0 || min_mapq > 0 ||
               min_read_length > 0 || !expression.empty();
    }
};

}  // namespace husk
