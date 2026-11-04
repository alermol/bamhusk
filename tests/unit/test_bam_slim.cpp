#include "bam_slim.hpp"
#include "test_harness.hpp"
#include "test_helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::string kHeader =
    "@HD\tVN:1.6\tSO:coordinate\n"
    "@SQ\tSN:chr1\tLN:1000\n"
    "@RG\tID:s1\tSM:sample1\n"
    "@PG\tID:aligner\tPN:test\n"
    "@CO\tdrop me\n";

const std::vector<std::string> kRecords = {
    "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1\tNM:i:0",
    "r2\t0\tchr1\t10\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1\tNM:i:1",
};

husk_test::BamPtr read_first_record(const std::filesystem::path &path) {
    husk_test::SamFilePtr input(sam_open(path.string().c_str(), "r"));
    if (!input) {
        throw std::runtime_error("cannot open BAM");
    }
    husk::HeaderPtr header(sam_hdr_read(input.get()));
    if (!header) {
        throw std::runtime_error("cannot read header");
    }
    husk_test::BamPtr record(bam_init1());
    if (sam_read1(input.get(), header.get(), record.get()) < 0) {
        throw std::runtime_error("cannot read record");
    }
    return record;
}

bool has_temporary_files(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".tmp.") !=
            std::string::npos) {
            return true;
        }
    }
    return false;
}

husk::OutputOptions output_for(
    const std::filesystem::path &input,
    const std::filesystem::path &output
) {
    husk::OutputOptions options;
    options.input = input;
    options.output = output;
    return options;
}

}  // namespace

void test_bam_slim() {
    husk_test::TempDir directory;
    const auto input = directory.path / "in.bam";
    husk_test::write_bam(input, kHeader, kRecords);

    {
        const auto output = directory.path / "out.bam";
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(input, output), husk::SlimOptions{}, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 2);
        CHECK(std::filesystem::exists(output));
        CHECK(!has_temporary_files(directory.path));

        const std::string header = husk_test::read_header_text(output);
        CHECK(header.find("@PG") == std::string::npos);
        CHECK(header.find("@CO") == std::string::npos);
        CHECK(header.find("SO:coordinate") != std::string::npos);

        husk_test::BamPtr record = read_first_record(output);
        CHECK_EQ(
            std::string(bam_get_qname(record.get())), std::string("*")
        );
        CHECK(bam_aux_get(record.get(), "RG") != nullptr);
        CHECK(bam_aux_get(record.get(), "NM") == nullptr);

        std::string error;
        CHECK(husk::verify_bam(output, 2, false, error));
        CHECK(error.empty());
    }

    {
        const auto output = directory.path / "existing.bam";
        husk_test::write_bam(output, kHeader, kRecords);
        std::filesystem::path stale_index = output;
        stale_index += ".csi";
        {
            std::FILE *index_file = std::fopen(stale_index.string().c_str(), "w");
            CHECK(index_file != nullptr);
            if (index_file != nullptr) {
                std::fputs("stale\n", index_file);
                std::fclose(index_file);
            }
        }
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(input, output), husk::SlimOptions{}, nullptr
            ),
            2
        );
        husk::OutputOptions forced = output_for(input, output);
        forced.force = true;
        CHECK_EQ(husk::write_slim_bam(forced, husk::SlimOptions{}, nullptr), 0);
        CHECK(!std::filesystem::exists(stale_index));
    }

    {
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(input, input), husk::SlimOptions{}, nullptr
            ),
            2
        );
    }

    {
        const auto malformed = directory.path / "malformed.bam";
        husk_test::write_bam(
            malformed,
            kHeader,
            {"r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:i:1"}
        );
        const auto output = directory.path / "malformed-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(malformed, output), husk::SlimOptions{}, nullptr
            ),
            1
        );
        CHECK(!std::filesystem::exists(output));
        CHECK(!has_temporary_files(directory.path));
    }

    {
        const auto unsorted = directory.path / "unsorted.bam";
        husk_test::write_bam(
            unsorted,
            kHeader,
            {
                "r1\t0\tchr1\t20\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1",
                "r2\t0\tchr1\t10\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1",
            }
        );
        const auto output = directory.path / "unsorted-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(unsorted, output), husk::SlimOptions{}, nullptr
            ),
            1
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto multi = directory.path / "multi.bam";
        husk_test::write_bam(
            multi,
            "@HD\tVN:1.6\tSO:coordinate\n"
            "@SQ\tSN:chr1\tLN:1000\n"
            "@RG\tID:a\tSM:alpha\n"
            "@RG\tID:b\tSM:beta\n",
            {
                "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:a",
                "r2\t0\tchr1\t2\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:b",
            }
        );
        husk::SlimOptions options;
        options.drop_rg = true;
        const auto output = directory.path / "multi-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(output_for(multi, output), options, nullptr),
            2
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto output = directory.path / "drop-rg.bam";
        husk::SlimOptions options;
        options.drop_rg = true;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            0
        );
        husk_test::BamPtr record = read_first_record(output);
        CHECK(bam_aux_get(record.get(), "RG") == nullptr);
    }

    {
        const auto output = directory.path / "indexed.bam";
        husk::SlimOptions options;
        options.index = true;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            0
        );
        CHECK(std::filesystem::exists(output));
        const auto index_path = directory.path / "indexed.bam.csi";
        CHECK(std::filesystem::exists(index_path));
        std::string error;
        CHECK(husk::verify_bam(output, 2, true, error));
        CHECK(error.empty());

        std::filesystem::resize_file(index_path, 8);
        error.clear();
        CHECK(!husk::verify_bam(output, 2, true, error));
        CHECK(!error.empty());
    }

    {
        const auto truncated = directory.path / "truncated.bam";
        husk_test::write_bam(truncated, kHeader, kRecords);
        const auto size = std::filesystem::file_size(truncated);
        std::filesystem::resize_file(truncated, size / 2);
        std::string error;
        CHECK(!husk::verify_bam(truncated, 2, false, error));
        CHECK(!error.empty());
    }

    {
        const auto output = directory.path / "compressed.bam";
        husk::SlimOptions options;
        options.compression = 0;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            0
        );
        options.compression = 9;
        const auto output2 = directory.path / "compressed2.bam";
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output2), options, nullptr),
            0
        );
        std::string error;
        CHECK(husk::verify_bam(output, 2, false, error));
        CHECK(husk::verify_bam(output2, 2, false, error));
    }

    {
        const auto empty = directory.path / "empty.bam";
        husk_test::write_bam(empty, kHeader, {});
        const auto output = directory.path / "empty-out.bam";
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(empty, output), husk::SlimOptions{}, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 0);
        std::string error;
        CHECK(husk::verify_bam(output, 0, false, error));
    }

    {
        const auto no_hd = directory.path / "no-hd.bam";
        husk_test::write_bam(
            no_hd,
            "@SQ\tSN:chr1\tLN:1000\n",
            {"r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII"}
        );
        const auto output = directory.path / "no-hd-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(no_hd, output), husk::SlimOptions{}, nullptr
            ),
            0
        );
        CHECK(husk_test::read_header_text(output).find("@SQ") !=
              std::string::npos);
    }

    {
        const auto no_usable = directory.path / "no-usable.bam";
        husk_test::write_bam(
            no_usable, "@PG\tID:aligner\n", {}
        );
        const auto output = directory.path / "no-usable-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(no_usable, output), husk::SlimOptions{}, nullptr
            ),
            1
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto text = directory.path / "plain.sam";
        {
            std::FILE *file = std::fopen(text.string().c_str(), "w");
            CHECK(file != nullptr);
            if (file != nullptr) {
                std::fputs("@HD\tVN:1.6\n@SQ\tSN:chr1\tLN:10\n", file);
                std::fclose(file);
            }
        }
        const auto output = directory.path / "sam-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(text, output), husk::SlimOptions{}, nullptr
            ),
            1
        );
    }

    {
        const auto output = directory.path / "verified.bam";
        husk::SlimOptions options;
        options.verify = true;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            0
        );
    }

    {
        const auto output = directory.path / "no-seq.bam";
        husk::SlimOptions options;
        options.drop_seq = true;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            0
        );
        husk_test::BamPtr record = read_first_record(output);
        CHECK_EQ(record->core.l_qseq, 0);
        CHECK_EQ(static_cast<int>(record->core.n_cigar), 1);
        std::string error;
        CHECK(husk::verify_bam(output, 2, false, error));
    }

    {
        const auto unmapped = directory.path / "unmapped.bam";
        husk_test::write_bam(
            unmapped,
            kHeader,
            {
                "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1",
                "r2\t4\t*\t0\t0\t*\t*\t0\t0\tACGT\tIIII",
            }
        );
        const auto output = directory.path / "unmapped-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(unmapped, output), husk::SlimOptions{}, nullptr
            ),
            0
        );
        std::string error;
        CHECK(husk::verify_bam(output, 2, false, error));
    }
}
