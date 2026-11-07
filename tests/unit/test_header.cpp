#include "header.hpp"
#include "test_harness.hpp"
#include "test_helpers.hpp"

#include <string>

void test_header() {
    const std::string header_text =
        "@HD\tVN:1.6\tSO:coordinate\n"
        "@SQ\tSN:chr1\tLN:1000\n"
        "@RG\tID:s1\tSM:sample1\n"
        "@PG\tID:aligner\tPN:test\n"
        "@CO\tdrop me\n";
    const std::string expected =
        "@HD\tVN:1.6\tSO:coordinate\n"
        "@SQ\tSN:chr1\tLN:1000\n"
        "@RG\tID:s1\tSM:sample1\n";

    CHECK_EQ(husk::slim_header_text(header_text), expected);
    CHECK(husk::is_kept_header_line("@HD\tVN:1.6"));
    CHECK(husk::is_kept_header_line("@SQ\tSN:chr1"));
    CHECK(husk::is_kept_header_line("@RG\tID:s1"));
    CHECK(husk::is_kept_header_line("@SQ"));
    CHECK(!husk::is_kept_header_line("@PG\tID:aligner"));
    CHECK(!husk::is_kept_header_line("@CO\tdrop me"));
    CHECK(!husk::is_kept_header_line("@SQX\tSN:chr1"));
    CHECK(!husk::is_kept_header_line("plain text"));

    CHECK_EQ(
        husk::slim_header_text("@SQ\tSN:chr1\tLN:1"),
        std::string("@SQ\tSN:chr1\tLN:1\n")
    );
    CHECK_EQ(
        husk::slim_header_text("@HD\tVN:1.6\r\n@PG\tID:x\r\n"),
        std::string("@HD\tVN:1.6\r\n")
    );
    CHECK(husk::slim_header_text("@PG\tID:x\n").empty());
    CHECK(husk::slim_header_text("").empty());

    husk::HeaderPtr header = husk_test::make_header(header_text);
    CHECK(header != nullptr);
    husk::HeaderPtr slim = husk::build_slim_header(header.get());
    CHECK(slim != nullptr);
    const std::string slim_text = sam_hdr_str(slim.get());
    const std::string marker = husk::husk_marker_line();
    CHECK(marker.rfind("@CO\tHUSK:1\tversion=", 0) == 0);
    CHECK(slim_text.find("@PG") == std::string::npos);
    CHECK(slim_text.find("@CO\tdrop me") == std::string::npos);
    CHECK(slim_text.find(marker) != std::string::npos);
    CHECK(slim_text.find("@CO", slim_text.find("@CO") + 3) == std::string::npos);
    CHECK(slim_text.find("SO:coordinate") != std::string::npos);

    husk::HeaderPtr no_sq = husk_test::make_header("@HD\tVN:1.6\n");
    CHECK(no_sq != nullptr);
    CHECK(husk::build_slim_header(no_sq.get()) == nullptr);

    husk::HeaderPtr rg_only = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\tSM:alpha\n"
    );
    CHECK(rg_only != nullptr);
    CHECK(husk::build_slim_header(rg_only.get()) == nullptr);

    husk::HeaderPtr two_samples = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\tSM:alpha\n"
        "@RG\tID:s2\tSM:beta\n"
    );
    CHECK_EQ(husk::count_distinct_samples(two_samples.get()), 2);

    husk::HeaderPtr one_sample = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\tSM:alpha\n"
        "@RG\tID:s2\tSM:alpha\n"
    );
    CHECK_EQ(husk::count_distinct_samples(one_sample.get()), 1);

    husk::HeaderPtr read_groups_without_samples = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\n"
        "@RG\tID:s2\n"
    );
    CHECK_EQ(husk::count_distinct_samples(read_groups_without_samples.get()), 2);

    husk::HeaderPtr mixed_samples = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\tSM:alpha\n"
        "@RG\tID:s2\n"
    );
    CHECK_EQ(husk::count_distinct_samples(mixed_samples.get()), 2);

    husk::HeaderPtr one_unknown_sample = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\n"
    );
    CHECK_EQ(husk::count_distinct_samples(one_unknown_sample.get()), 1);

    husk::HeaderPtr three_unknown_samples = husk_test::make_header(
        "@HD\tVN:1.6\n"
        "@RG\tID:s1\n"
        "@RG\tID:s2\n"
        "@RG\tID:s3\n"
    );
    CHECK_EQ(husk::count_distinct_samples(three_unknown_samples.get()), 3);

    husk::HeaderPtr no_read_groups =
        husk_test::make_header("@HD\tVN:1.6\n@SQ\tSN:chr1\tLN:10\n");
    CHECK_EQ(husk::count_distinct_samples(no_read_groups.get()), 0);

    {
        const auto samples = husk::read_group_samples(two_samples.get());
        CHECK_EQ(samples.size(), std::size_t{2});
        CHECK_EQ(samples.at("s1"), std::string("alpha"));
        CHECK_EQ(samples.at("s2"), std::string("beta"));
    }
    {
        const auto samples = husk::read_group_samples(one_sample.get());
        CHECK_EQ(samples.size(), std::size_t{2});
        CHECK_EQ(samples.at("s1"), std::string("alpha"));
        CHECK_EQ(samples.at("s2"), std::string("alpha"));
    }
    {
        const auto samples = husk::read_group_samples(mixed_samples.get());
        CHECK_EQ(samples.size(), std::size_t{1});
        CHECK_EQ(samples.at("s1"), std::string("alpha"));
        CHECK(samples.find("s2") == samples.end());
    }
    {
        const auto samples =
            husk::read_group_samples(read_groups_without_samples.get());
        CHECK(samples.empty());
    }
    {
        const auto samples = husk::read_group_samples(no_read_groups.get());
        CHECK(samples.empty());
    }
}
