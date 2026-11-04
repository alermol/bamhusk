#include "cli.hpp"
#include "test_harness.hpp"

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

    CHECK(!parse({"--threads", "0", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "-1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "10", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "-1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "x", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--threads", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--compression", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--unknown", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"-x", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"--force=1", "in.bam", "out.bam"}).error.empty());
    CHECK(!parse({"in.bam"}).error.empty());
    CHECK(!parse({"in.bam", "out.bam", "extra"}).error.empty());
    CHECK(!parse({"--drop-seq", "--keep-qual", "in.bam", "out.bam"})
               .error.empty());

    {
        const auto result = parse({"--", "--in.bam", "--out.bam"});
        CHECK(result.error.empty());
        CHECK_EQ(result.options.input.string(), std::string("--in.bam"));
        CHECK_EQ(result.options.output.string(), std::string("--out.bam"));
    }
}
