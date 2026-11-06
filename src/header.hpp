#pragma once

#include <htslib/sam.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace husk {

struct SamHeaderDeleter {
    void operator()(sam_hdr_t *header) const {
        if (header != nullptr) {
            sam_hdr_destroy(header);
        }
    }
};

using HeaderPtr = std::unique_ptr<sam_hdr_t, SamHeaderDeleter>;

bool is_kept_header_line(std::string_view line);
std::string slim_header_text(std::string_view text);
HeaderPtr build_slim_header(const sam_hdr_t *header);
int count_distinct_samples(const sam_hdr_t *header);
std::unordered_map<std::string, std::string> read_group_samples(
    const sam_hdr_t *header
);

}  // namespace husk
