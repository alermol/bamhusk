#include "cli.hpp"
#include "test_harness.hpp"

#include <htslib/sam.h>

#include <string>
#include <vector>

namespace {

husk::ParseResult parse(const std::vector<std::string> &args) {
    return husk::parse_args(args);
}

}  // namespace

void test_cli() {
    {
        const auto result = parse({"in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK(result.action == husk::Action::Run);
        CHECK_EQ(result.options.input.string(), std::string("in.bam"));
        CHECK_EQ(result.options.output.string(), std::string("out.bam"));
        CHECK(result.options.threads >= 1);
        CHECK_EQ(result.options.compression, 6);
        CHECK(!result.options.drop_seq);
        CHECK(!result.options.keep_qual);
        CHECK(!result.options.keep_name);
        CHECK(!result.options.drop_rg);
        CHECK(!result.options.index);
        CHECK(!result.options.force);
        CHECK(!result.options.verify);
        CHECK_EQ(result.options.filter.require_flags, 0);
        CHECK_EQ(result.options.filter.exclude_flags, 0);
        CHECK_EQ(result.options.filter.min_mapq, 0);
        CHECK_EQ(result.options.filter.min_read_length, std::int64_t{0});
        CHECK(result.options.filter.expression.empty());
        CHECK(!result.options.filter.enabled());
        CHECK(!result.options.downsample.enabled());
        CHECK(result.options.downsample.mode == husk::DownsampleMode::Total);
        CHECK_EQ(result.options.downsample.seed, std::uint64_t{0});
    }

    {
        const auto result = parse({"--help"});
        CHECK(result.error.empty());
        CHECK(result.action == husk::Action::Help);
    }
    {
        const auto result = parse({"-h"});
        CHECK(result.action == husk::Action::Help);
    }
    {
        const auto result = parse({"--version"});
        CHECK(result.error.empty());
        CHECK(result.action == husk::Action::Version);
    }

    {
        const auto result = parse({
            "--drop-seq", "--keep-name", "--drop-rg", "--index", "--force",
            "--verify", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(result.options.drop_seq);
        CHECK(result.options.keep_name);
        CHECK(result.options.drop_rg);
        CHECK(result.options.index);
        CHECK(result.options.force);
        CHECK(result.options.verify);
    }

    {
        const auto result = parse({"--threads", "7", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.threads, 7);
    }
    {
        const auto result = parse({"--threads=7", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.threads, 7);
    }
    {
        const auto result = parse({"-@", "7", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.threads, 7);
    }
    {
        const auto result = parse({"-@7", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.threads, 7);
    }
    {
        const auto result = parse({"--compression", "0", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.compression, 0);
    }
    {
        const auto result = parse({"--compression=9", "in.bam", "out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.compression, 9);
    }

    {
        const auto result = parse({
            "--require-flags", "UNMAP,SECONDARY", "--exclude-flags",
            "0x400", "--min-mapq", "20", "--min-read-length", "50",
            "--filter-expr", "mapq >= 20", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK_EQ(
            result.options.filter.require_flags,
            BAM_FUNMAP | BAM_FSECONDARY
        );
        CHECK_EQ(result.options.filter.exclude_flags, BAM_FDUP);
        CHECK_EQ(result.options.filter.min_mapq, 20);
        CHECK_EQ(result.options.filter.min_read_length, std::int64_t{50});
        CHECK_EQ(
            result.options.filter.expression,
            std::string("mapq >= 20")
        );
        CHECK(result.options.filter.enabled());
    }
    {
        const auto result = parse({
            "--require-flags=0x4", "--exclude-flags=UNMAP,DUP", "in.bam",
            "out.bam",
        });
        CHECK(result.error.empty());
        CHECK_EQ(result.options.filter.require_flags, BAM_FUNMAP);
        CHECK_EQ(
            result.options.filter.exclude_flags, BAM_FUNMAP | BAM_FDUP
        );
    }
    {
        const auto result = parse({
            "--min-mapq", "255", "--min-read-length", "0", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK_EQ(result.options.filter.min_mapq, 255);
        CHECK_EQ(result.options.filter.min_read_length, std::int64_t{0});
    }

    {
        const auto result = parse({
            "--downsample-fraction", "0.25", "--downsample-mode", "sample",
            "--downsample-seed", "123", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(result.options.downsample.fraction.has_value());
        CHECK_EQ(*result.options.downsample.fraction, 0.25);
        CHECK(!result.options.downsample.count.has_value());
        CHECK(result.options.downsample.mode == husk::DownsampleMode::Sample);
        CHECK_EQ(result.options.downsample.seed, std::uint64_t{123});
    }
    {
        const auto result = parse({
            "--downsample-count=100", "--downsample-mode=total", "in.bam",
            "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(!result.options.downsample.fraction.has_value());
        CHECK(result.options.downsample.count.has_value());
        CHECK_EQ(*result.options.downsample.count, std::int64_t{100});
        CHECK(result.options.downsample.mode == husk::DownsampleMode::Total);
    }
    {
        const auto result = parse({
            "--downsample-fraction", "0.5", "--downsample-mode", "sample-wise",
            "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(result.options.downsample.mode == husk::DownsampleMode::Sample);
    }
    {
        const auto result = parse({
            "--downsample-count", "1", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(result.options.downsample.mode == husk::DownsampleMode::Total);
        CHECK(!result.options.downsample_mode_set);
        CHECK(!result.options.downsample_seed_set);
    }
    {
        const auto result = parse({
            "--downsample-fraction=1", "in.bam", "out.bam",
        });
        CHECK(result.error.empty());
        CHECK(result.options.downsample.fraction.has_value());
        CHECK_EQ(*result.options.downsample.fraction, 1.0);
    }

    CHECK(!parse({"--threads", "0", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "-1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "10", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "-1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "x", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--require-flags", "BOGUS", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--require-flags", "0x10000", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--require-flags", "0x100000000", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--require-flags", "4x", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--exclude-flags", "-1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--min-mapq", "256", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--min-mapq", "-1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--min-mapq", "x", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--min-read-length", "-1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--min-read-length", "x", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--filter-expr=", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--unknown", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"-x", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--force=1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"in.bam"}).error.empty());
    CHECK(!parse({"in.bam", "out.bam", "extra"}).error.empty());
    CHECK(!parse({"--drop-seq", "--keep-qual", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({
              "--downsample-fraction", "0.5", "--downsample-count", "10",
              "in.bam", "out.bam",
          })
               .error.empty());
    CHECK(!parse({"--downsample-fraction", "0", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-fraction", "-0.1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-fraction", "1.1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-fraction", "nan", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-fraction", "x", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-count", "0", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-count", "-1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({
              "--downsample-count", "9223372036854775808", "in.bam",
              "out.bam",
          })
               .error.empty());
    CHECK(!parse({"--downsample-count", "x", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-seed", "-1", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-seed", "x", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-mode", "sample", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-seed", "0", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-mode", "bogus", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-mode", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--downsample-fraction", "in.bam", "out.bam"})
               .error.empty());
    CHECK(!parse({"--downsample-count", "in.bam", "out.bam"}).error.empty());

    {
        const auto result = parse({"--", "--in.bam", "--out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.input.string(), std::string("--in.bam"));
        CHECK_EQ(result.options.output.string(), std::string("--out.bam"));
    }
}
