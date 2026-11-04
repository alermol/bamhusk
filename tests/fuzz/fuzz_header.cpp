#include "header.hpp"

#include <htslib/hts.h>

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    hts_set_log_level(HTS_LOG_OFF);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::string text(reinterpret_cast<const char *>(data), size);
    const std::string slim = husk::slim_header_text(text);
    if (slim.empty()) {
        return 0;
    }

    sam_hdr_t *header = sam_hdr_parse(slim.size(), slim.c_str());
    if (header != nullptr) {
        sam_hdr_destroy(header);
    }
    return 0;
}
