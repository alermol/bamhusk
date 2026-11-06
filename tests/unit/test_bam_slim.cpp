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

const std::string kDownsampleHeader =
    "@HD\tVN:1.6\tSO:coordinate\n"
    "@SQ\tSN:chr1\tLN:1000\n"
    "@RG\tID:a\tSM:alpha\n"
    "@RG\tID:a2\tSM:alpha\n"
    "@RG\tID:b\tSM:beta\n"
    "@RG\tID:c\n";

std::vector<std::string> downsample_records() {
    std::vector<std::string> records;
    records.reserve(20);
    for (int index = 0; index < 20; ++index) {
        std::string name;
        std::string tag;
        if (index < 10) {
            name = "alpha" + std::to_string(index);
            tag = index % 2 == 0 ? "RG:Z:a" : "RG:Z:a2";
        } else if (index < 16) {
            name = "beta" + std::to_string(index - 10);
            tag = "RG:Z:b";
        } else {
            name = "unknown" + std::to_string(index - 16);
            if (index == 16) {
                tag = "RG:Z:missing";
            } else if (index == 17) {
                tag = "RG:Z:c";
            }
        }
        std::string line = name + "\t0\tchr1\t" +
                           std::to_string(index + 1) +
                           "\t60\t4M\t*\t0\t0\tACGT\tIIII";
        if (!tag.empty()) {
            line += "\t";
            line += tag;
        }
        records.push_back(std::move(line));
    }
    return records;
}

std::vector<std::string> read_names(const std::filesystem::path &path) {
    husk_test::SamFilePtr input(sam_open(path.string().c_str(), "r"));
    if (!input) {
        throw std::runtime_error("cannot open BAM");
    }
    husk::HeaderPtr header(sam_hdr_read(input.get()));
    if (!header) {
        throw std::runtime_error("cannot read header");
    }
    husk_test::BamPtr record(bam_init1());
    if (!record) {
        throw std::runtime_error("cannot allocate a BAM record");
    }

    std::vector<std::string> names;
    while (true) {
        const int status = sam_read1(input.get(), header.get(), record.get());
        if (status == -1) {
            break;
        }
        if (status < -1) {
            throw std::runtime_error("cannot read record");
        }
        names.emplace_back(bam_get_qname(record.get()));
    }
    return names;
}

std::size_t count_prefix(
    const std::vector<std::string> &names,
    const std::string &prefix
) {
    std::size_t count = 0;
    for (const std::string &name : names) {
        if (name.rfind(prefix, 0) == 0) {
            ++count;
        }
    }
    return count;
}

bool is_subsequence(
    const std::vector<std::string> &subset,
    const std::vector<std::string> &superset
) {
    std::size_t next = 0;
    for (const std::string &name : subset) {
        while (next < superset.size() && superset[next] != name) {
            ++next;
        }
        if (next == superset.size()) {
            return false;
        }
        ++next;
    }
    return true;
}

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

    {
        const auto header_only = directory.path / "header-only.bam";
        husk_test::write_bam(
            header_only, "@HD\tVN:1.6\tSO:coordinate\n", {}
        );
        const auto output = directory.path / "header-only-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(header_only, output), husk::SlimOptions{}, nullptr
            ),
            1
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto mixed = directory.path / "mixed-sample.bam";
        husk_test::write_bam(
            mixed,
            "@HD\tVN:1.6\tSO:coordinate\n"
            "@SQ\tSN:chr1\tLN:1000\n"
            "@RG\tID:a\tSM:alpha\n"
            "@RG\tID:b\n",
            {
                "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:a",
                "r2\t0\tchr1\t2\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:b",
            }
        );
        husk::SlimOptions options;
        options.drop_rg = true;
        const auto output = directory.path / "mixed-sample-out.bam";
        CHECK_EQ(
            husk::write_slim_bam(output_for(mixed, output), options, nullptr),
            2
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        husk::SlimOptions options;
        options.drop_seq = true;
        options.keep_qual = true;
        const auto output = directory.path / "invalid-combo.bam";
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, nullptr),
            2
        );
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto collision = directory.path / "collision.bam.csi";
        husk_test::write_bam(collision, kHeader, kRecords);
        const auto before = std::filesystem::file_size(collision);
        const auto output = directory.path / "collision.bam";
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(collision, output), husk::SlimOptions{}, nullptr
            ),
            2
        );
        CHECK(std::filesystem::exists(collision));
        CHECK_EQ(std::filesystem::file_size(collision), before);
        CHECK(!std::filesystem::exists(output));
    }

    {
        const auto output = directory.path / "index-directory.bam";
        {
            std::FILE *file = std::fopen(output.string().c_str(), "w");
            CHECK(file != nullptr);
            if (file != nullptr) {
                std::fputs("old\n", file);
                std::fclose(file);
            }
        }
        const auto index_path = directory.path / "index-directory.bam.csi";
        CHECK(std::filesystem::create_directory(index_path));
        const auto before = std::filesystem::file_size(output);
        husk::OutputOptions forced = output_for(input, output);
        forced.force = true;
        husk::SlimOptions options;
        options.index = true;
        CHECK_EQ(
            husk::write_slim_bam(forced, options, nullptr),
            1
        );
        CHECK_EQ(std::filesystem::file_size(output), before);
        std::filesystem::remove_all(index_path);
    }

    {
        const auto output = directory.path / "sibling.bam";
        const auto input_path =
            directory.path /
            ("sibling.bam.tmp." + std::to_string(husk::process_id()) + ".csi");
        husk_test::write_bam(input_path, kHeader, kRecords);
        const auto before = std::filesystem::file_size(input_path);
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(input_path, output), husk::SlimOptions{}, nullptr
            ),
            0
        );
        CHECK(std::filesystem::exists(input_path));
        CHECK_EQ(std::filesystem::file_size(input_path), before);
        std::string error;
        CHECK(husk::verify_bam(output, 2, false, error));
    }

    {
        const auto output = directory.path / "symlink-temp.bam";
        const auto victim = directory.path / "missing-victim";
        const auto base =
            directory.path /
            ("symlink-temp.bam.tmp." + std::to_string(husk::process_id()));
        std::error_code link_error;
        std::filesystem::create_symlink(victim, base, link_error);
        if (!link_error) {
            std::filesystem::create_symlink(
                victim, base.string() + ".csi", link_error
            );
        }
        if (!link_error) {
            husk::SlimOptions options;
            options.index = true;
            CHECK_EQ(
                husk::write_slim_bam(
                    output_for(input, output), options, nullptr
                ),
                0
            );
            CHECK(!std::filesystem::exists(victim));
            CHECK(std::filesystem::exists(output));
            CHECK(std::filesystem::exists(output.string() + ".csi"));
        }
    }

    const auto downsample_input = directory.path / "downsample.bam";
    husk_test::write_bam(
        downsample_input, kDownsampleHeader, downsample_records()
    );
    const auto downsample_names = read_names(downsample_input);
    CHECK_EQ(downsample_names.size(), std::size_t{20});

    {
        const auto output = directory.path / "downsample-total-count.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.count = 6;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 6);
        CHECK_EQ(stats.input_reads, 20);
        const auto names = read_names(output);
        CHECK_EQ(names.size(), std::size_t{6});
        CHECK(is_subsequence(names, downsample_names));
    }

    {
        const auto output = directory.path / "downsample-sample-count.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.count = 3;
        options.downsample.mode = husk::DownsampleMode::Sample;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 9);
        CHECK_EQ(stats.input_reads, 20);
        const auto names = read_names(output);
        CHECK_EQ(count_prefix(names, "alpha"), std::size_t{3});
        CHECK_EQ(count_prefix(names, "beta"), std::size_t{3});
        CHECK_EQ(count_prefix(names, "unknown"), std::size_t{3});
        CHECK(is_subsequence(names, downsample_names));
    }

    {
        const auto output = directory.path / "downsample-total-fraction.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.fraction = 0.25;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 5);
        CHECK_EQ(stats.input_reads, 20);
        CHECK_EQ(read_names(output).size(), std::size_t{5});
    }

    {
        const auto output = directory.path / "downsample-sample-fraction.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.fraction = 0.5;
        options.downsample.mode = husk::DownsampleMode::Sample;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 10);
        const auto names = read_names(output);
        CHECK_EQ(count_prefix(names, "alpha"), std::size_t{5});
        CHECK_EQ(count_prefix(names, "beta"), std::size_t{3});
        CHECK_EQ(count_prefix(names, "unknown"), std::size_t{2});
    }

    {
        const auto output = directory.path / "downsample-all.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.count = 100;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 20);
        CHECK_EQ(stats.input_reads, 20);
        CHECK_EQ(read_names(output).size(), std::size_t{20});
    }

    {
        const auto output = directory.path / "downsample-all-fraction.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.fraction = 1.0;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 20);
        CHECK_EQ(stats.input_reads, 20);
        CHECK_EQ(read_names(output).size(), std::size_t{20});
    }

    {
        const auto output1 = directory.path / "downsample-seed-1.bam";
        const auto output2 = directory.path / "downsample-seed-2.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.count = 5;
        options.downsample.seed = 4242;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output1), options, nullptr
            ),
            0
        );
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output2), options, nullptr
            ),
            0
        );
        CHECK(read_names(output1) == read_names(output2));
    }

    {
        const auto output = directory.path / "downsample-verify.bam";
        husk::SlimOptions options;
        options.downsample.count = 7;
        options.verify = true;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(downsample_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 7);
    }

    {
        const auto empty_input = directory.path / "downsample-empty.bam";
        husk_test::write_bam(empty_input, kDownsampleHeader, {});
        const auto output = directory.path / "downsample-empty-out.bam";
        husk::SlimOptions options;
        options.downsample.count = 1;
        options.verify = true;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(empty_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 0);
        CHECK_EQ(stats.input_reads, 0);
    }

    {
        const auto output = directory.path / "downsample-drop-rg.bam";
        husk::SlimOptions options;
        options.drop_rg = true;
        options.downsample.count = 1;
        options.downsample.mode = husk::DownsampleMode::Sample;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(output_for(input, output), options, &stats),
            0
        );
        CHECK_EQ(stats.reads, 1);
        husk_test::BamPtr record = read_first_record(output);
        CHECK(bam_aux_get(record.get(), "RG") == nullptr);
    }

    {
        const auto no_rg_input = directory.path / "downsample-no-rg.bam";
        husk_test::write_bam(
            no_rg_input,
            "@HD\tVN:1.6\tSO:coordinate\n"
            "@SQ\tSN:chr1\tLN:1000\n",
            {
                "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII",
                "r2\t0\tchr1\t2\t60\t4M\t*\t0\t0\tACGT\tIIII",
            }
        );
        const auto output = directory.path / "downsample-no-rg-out.bam";
        husk::SlimOptions options;
        options.keep_name = true;
        options.downsample.count = 1;
        options.downsample.mode = husk::DownsampleMode::Sample;
        husk::SlimStats stats;
        CHECK_EQ(
            husk::write_slim_bam(
                output_for(no_rg_input, output), options, &stats
            ),
            0
        );
        CHECK_EQ(stats.reads, 1);
        CHECK_EQ(stats.input_reads, 2);
    }
}
