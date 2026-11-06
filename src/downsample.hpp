#pragma once

#include <cstdint>
#include <optional>

namespace husk {

enum class DownsampleMode : std::uint8_t {
    Total,
    Sample,
};

struct DownsampleOptions {
    std::optional<double> fraction;
    std::optional<std::int64_t> count;
    DownsampleMode mode = DownsampleMode::Total;
    std::uint64_t seed = 0;

    bool enabled() const {
        return fraction.has_value() || count.has_value();
    }
};

}  // namespace husk
