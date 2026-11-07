#pragma once

#include "downsample.hpp"
#include "filter.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include <htslib/sam.h>

namespace husk {

struct OutputOptions {
    bool force = false;
    std::filesystem::path input;
    std::filesystem::path output;
};

struct SlimOptions {
    bool drop_seq = false;
    bool keep_qual = false;
    bool keep_name = false;
    bool drop_rg = false;
    bool index = false;
    bool verify = false;
    int threads = 1;
    int compression = 6;
    FilterOptions filter;
    DownsampleOptions downsample;
};

struct SlimStats {
    std::int64_t reads = 0;
    std::int64_t input_reads = 0;
    std::int64_t input_bytes = -1;
    std::int64_t output_bytes = -1;
};

struct RecordPolicy {
    bool drop_seq = false;
    bool keep_qual = false;
    bool keep_name = false;
    bool drop_rg = false;
};

bool transform_record(
    const bam1_t *source,
    bam1_t *destination,
    const RecordPolicy &policy,
    std::string &error
);

int write_slim_bam(
    const OutputOptions &output,
    const SlimOptions &options,
    SlimStats *stats = nullptr
);

bool verify_bam(
    const std::filesystem::path &path,
    std::int64_t expected_reads,
    bool expect_index,
    std::string &error
);

}  // namespace husk
