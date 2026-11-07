#include "bam_slim.hpp"

#include "header.hpp"
#include "platform.hpp"

#include <htslib/hts.h>
#include <htslib/sam.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace husk {
namespace {

struct SamFileDeleter {
    void operator()(samFile *file) const {
        if (file != nullptr) {
            sam_close(file);
        }
    }
};

struct BamDeleter {
    void operator()(bam1_t *record) const {
        if (record != nullptr) {
            bam_destroy1(record);
        }
    }
};

using SamFilePtr = std::unique_ptr<samFile, SamFileDeleter>;
using BamPtr = std::unique_ptr<bam1_t, BamDeleter>;

struct TempFiles {
    std::filesystem::path bam;
    std::filesystem::path index;

    ~TempFiles() {
        if (!bam.empty()) {
            remove_file(bam);
        }
        if (!index.empty()) {
            remove_file(index);
        }
    }

    void release_bam() {
        bam.clear();
    }

    void release_index() {
        index.clear();
    }
};

void report(const std::string &message) {
    std::fprintf(stderr, "husk: %s\n", message.c_str());
}

bool apply_filter_expression(
    samFile *file,
    const FilterOptions &filter,
    std::string &error
) {
    if (filter.expression.empty()) {
        return true;
    }
    if (hts_set_filter_expression(file, filter.expression.c_str()) < 0) {
        error = "invalid --filter-expr '" + filter.expression + "'";
        return false;
    }
    return true;
}

bool record_passes_filter(const bam1_t *record, const FilterOptions &filter) {
    if ((record->core.flag & filter.require_flags) != filter.require_flags) {
        return false;
    }
    if ((record->core.flag & filter.exclude_flags) != 0) {
        return false;
    }
    if (record->core.qual < filter.min_mapq) {
        return false;
    }
    if (filter.min_read_length > 0) {
        const hts_pos_t length = bam_cigar2qlen(
            static_cast<int>(record->core.n_cigar), bam_get_cigar(record)
        );
        if (length < filter.min_read_length) {
            return false;
        }
    }
    return true;
}

bool remove_unwanted_aux(
    bam1_t *record,
    bool keep_rg,
    bool keep_cg,
    std::string &error
) {
    errno = 0;
    std::uint8_t *aux = bam_aux_first(record);
    if (aux == nullptr) {
        if (errno == ENOENT) {
            return true;
        }
        error = "read has malformed auxiliary data";
        return false;
    }

    while (aux != nullptr) {
        const bool is_rg = aux[-2] == 'R' && aux[-1] == 'G';
        const bool is_cg = aux[-2] == 'C' && aux[-1] == 'G';
        const bool keep_rg_tag = is_rg && keep_rg;
        const bool keep_cg_tag =
            is_cg && keep_cg && aux[0] == 'B' &&
            (aux[1] == 'I' || aux[1] == 'i');

        if (keep_rg_tag) {
            const std::uint8_t *end = record->data + record->l_data;
            if (aux[0] != 'Z' ||
                std::memchr(aux + 1, 0, static_cast<std::size_t>(end - aux - 1)) ==
                    nullptr) {
                error = "read has a malformed RG tag";
                return false;
            }
        }

        if (keep_rg_tag || keep_cg_tag) {
            aux = bam_aux_next(record, aux);
        } else {
            aux = bam_aux_remove(record, aux);
        }
        if (aux == nullptr && errno != ENOENT) {
            error = "read has malformed auxiliary data";
            return false;
        }
    }
    return true;
}

bool check_coordinate_order(
    const bam1_t *record,
    std::int32_t &last_tid,
    std::int64_t &last_position,
    std::string &error
) {
    const std::int32_t tid = record->core.tid;
    const std::int64_t position = record->core.pos;

    if (last_tid != -2) {
        if (last_tid == -1) {
            if (tid != -1) {
                error = "input is not coordinate-sorted";
                return false;
            }
        } else if (tid == -1) {
            // Unmapped records may follow mapped records.
        } else if (tid < last_tid || (tid == last_tid && position < last_position)) {
            error = "input is not coordinate-sorted";
            return false;
        }
    }

    last_tid = tid;
    last_position = position;
    return true;
}

struct SampleGroups {
    std::unordered_map<std::string, int> by_read_group;
    int unknown = 0;
    int count = 1;
};

SampleGroups make_sample_groups(const sam_hdr_t *header) {
    SampleGroups groups;
    const auto rg_samples = read_group_samples(header);
    std::unordered_map<std::string, int> sample_groups;
    int next_group = 1;
    for (const auto &[read_group, sample] : rg_samples) {
        auto found = sample_groups.find(sample);
        if (found == sample_groups.end()) {
            found = sample_groups.emplace(sample, next_group++).first;
        }
        groups.by_read_group.emplace(read_group, found->second);
    }
    groups.count = next_group;
    return groups;
}

int group_for_record(
    const bam1_t *record,
    bool per_sample,
    const SampleGroups &groups,
    std::string &read_group
) {
    if (!per_sample) {
        return 0;
    }

    const std::uint8_t *rg = bam_aux_get(record, "RG");
    if (rg == nullptr) {
        return groups.unknown;
    }

    const std::uint8_t *data_end = record->data + record->l_data;
    if (rg < record->data || rg >= data_end || rg[0] != 'Z') {
        return groups.unknown;
    }
    const std::uint8_t *value = rg + 1;
    if (value >= data_end ||
        std::memchr(value, 0, static_cast<std::size_t>(data_end - value)) ==
            nullptr) {
        return groups.unknown;
    }

    read_group.assign(reinterpret_cast<const char *>(value));
    const auto found = groups.by_read_group.find(read_group);
    if (found == groups.by_read_group.end()) {
        return groups.unknown;
    }
    return found->second;
}

bool count_reads_per_group(
    const std::filesystem::path &path,
    const SampleGroups &groups,
    bool per_sample,
    const FilterOptions &filter,
    int threads,
    std::vector<std::int64_t> &counts,
    std::string &error
) {
    SamFilePtr file(sam_open(path.string().c_str(), "r"));
    if (!file) {
        error = "cannot reopen '" + path.string() + "'";
        return false;
    }
    if (threads > 1 && hts_set_threads(file.get(), threads) != 0) {
        error = "cannot set input threads";
        return false;
    }
    if (!apply_filter_expression(file.get(), filter, error)) {
        return false;
    }

    HeaderPtr header(sam_hdr_read(file.get()));
    if (!header) {
        error = "cannot read the header of '" + path.string() + "'";
        return false;
    }

    BamPtr record(bam_init1());
    if (!record) {
        error = "cannot allocate a BAM record";
        return false;
    }

    counts.assign(static_cast<std::size_t>(groups.count), 0);
    std::string read_group;
    while (true) {
        if (interrupted()) {
            error = "interrupted";
            return false;
        }

        const int status = sam_read1(file.get(), header.get(), record.get());
        if (status == -1) {
            break;
        }
        if (status < -1) {
            error =
                filter.expression.empty()
                    ? "error reading '" + path.string() + "'"
                    : "error evaluating --filter-expr '" + filter.expression +
                          "' on '" + path.string() + "'";
            return false;
        }
        if (!record_passes_filter(record.get(), filter)) {
            continue;
        }

        const int group =
            group_for_record(record.get(), per_sample, groups, read_group);
        if (group < 0 || static_cast<std::size_t>(group) >= counts.size()) {
            error = "internal sample grouping error";
            return false;
        }
        ++counts[static_cast<std::size_t>(group)];
    }
    return true;
}

std::uint64_t random_below(std::mt19937_64 &rng, std::uint64_t bound) {
    // Rejection sampling avoids the modulo bias that a plain rng() % bound
    // would introduce. bound must be greater than zero.
    const std::uint64_t threshold = (std::uint64_t{0} - bound) % bound;
    std::uint64_t value = rng();
    while (value < threshold) {
        value = rng();
    }
    return value % bound;
}

std::int64_t target_for_group(
    const DownsampleOptions &options,
    std::int64_t group_count
) {
    if (options.fraction.has_value()) {
        const auto target = static_cast<std::int64_t>(
            std::llround(*options.fraction * static_cast<double>(group_count))
        );
        return std::min(group_count, std::max<std::int64_t>(target, 0));
    }
    if (options.count.has_value()) {
        return std::min(*options.count, group_count);
    }
    return group_count;
}

}  // namespace

bool transform_record(
    const bam1_t *source,
    bam1_t *destination,
    const RecordPolicy &policy,
    std::string &error
) {
    if (source == nullptr || destination == nullptr) {
        error = "null BAM record";
        return false;
    }
    if (source == destination) {
        error = "source and destination must be different records";
        return false;
    }
    if (policy.drop_seq && policy.keep_qual) {
        error = "--keep-qual requires SEQ";
        return false;
    }

    if (!policy.drop_seq && source->core.l_qseq > 0 &&
        !(source->core.flag & BAM_FUNMAP)) {
        const hts_pos_t query_length = bam_cigar2qlen(
            static_cast<int>(source->core.n_cigar), bam_get_cigar(source)
        );
        if (query_length != source->core.l_qseq) {
            error = "CIGAR and query sequence lengths differ";
            return false;
        }
    }

    // A CIGAR longer than 65535 operations is represented by a two-operation
    // placeholder plus a CG:B:I tag. Preserve CG or the real CIGAR is lost.
    bool keep_cg = false;
    if (source->core.n_cigar == 2) {
        errno = 0;
        const std::uint8_t *cg = bam_aux_get(source, "CG");
        if (cg == nullptr) {
            if (errno != ENOENT) {
                error = "read has a malformed CG tag";
                return false;
            }
        } else if (cg[0] == 'B' && (cg[1] == 'I' || cg[1] == 'i')) {
            keep_cg = true;
        }
    }

    if (!policy.drop_rg) {
        errno = 0;
        const std::uint8_t *rg = bam_aux_get(source, "RG");
        if (rg == nullptr) {
            if (errno != ENOENT) {
                error = "read has a malformed RG tag";
                return false;
            }
        } else if (rg[0] != 'Z') {
            error = "read has a malformed RG tag";
            return false;
        }
    }

    if (bam_copy1(destination, source) == nullptr) {
        error = "cannot copy a BAM record";
        return false;
    }

    if (!policy.keep_name && bam_set_qname(destination, "*") < 0) {
        error = "cannot rewrite a read name";
        return false;
    }

    if (policy.drop_seq) {
        const int sequence_length = destination->core.l_qseq;
        if (sequence_length > 0) {
            std::uint8_t *data = destination->data;
            std::uint8_t *sequence = bam_get_seq(destination);
            std::uint8_t *aux = bam_get_aux(destination);
            const std::size_t sequence_offset =
                static_cast<std::size_t>(sequence - data);
            const std::size_t aux_offset =
                static_cast<std::size_t>(aux - data);
            if (sequence_offset > aux_offset ||
                aux_offset >
                    static_cast<std::size_t>(destination->l_data)) {
                error = "malformed BAM record";
                return false;
            }
            const std::size_t tail =
                static_cast<std::size_t>(destination->l_data) - aux_offset;
            std::memmove(sequence, aux, tail);
            destination->l_data =
                static_cast<int>(sequence_offset + tail);
            destination->core.l_qseq = 0;
        }
    } else if (!policy.keep_qual && destination->core.l_qseq > 0) {
        std::memset(
            bam_get_qual(destination),
            0xff,
            static_cast<std::size_t>(destination->core.l_qseq)
        );
    }

    if (!remove_unwanted_aux(
            destination, !policy.drop_rg, keep_cg, error
        )) {
        return false;
    }
    return true;
}

int write_slim_bam(
    const OutputOptions &output,
    const SlimOptions &options,
    SlimStats *stats
) {
    if (options.threads < 1) {
        report("--threads must be at least 1");
        return 2;
    }
    if (options.compression < 0 || options.compression > 9) {
        report("--compression must be between 0 and 9");
        return 2;
    }
    if (options.filter.require_flags < 0 ||
        options.filter.require_flags > 0xffff ||
        options.filter.exclude_flags < 0 ||
        options.filter.exclude_flags > 0xffff) {
        report("filter flags must be 16-bit values");
        return 2;
    }
    if (options.filter.min_mapq < 0 || options.filter.min_mapq > 255) {
        report("--min-mapq must be between 0 and 255");
        return 2;
    }
    if (options.filter.min_read_length < 0) {
        report("--min-read-length must be non-negative");
        return 2;
    }
    if (options.drop_seq && options.keep_qual) {
        report("--keep-qual requires SEQ; drop --drop-seq");
        return 2;
    }
    if (options.downsample.fraction.has_value() &&
        options.downsample.count.has_value()) {
        report(
            "use only one of --downsample-fraction and --downsample-count"
        );
        return 2;
    }
    if (options.downsample.fraction.has_value()) {
        const double fraction = *options.downsample.fraction;
        if (!(fraction > 0.0 && fraction <= 1.0)) {
            report("--downsample-fraction must be greater than 0 and at most 1");
            return 2;
        }
    }
    if (options.downsample.count.has_value() &&
        *options.downsample.count < 1) {
        report("--downsample-count must be at least 1");
        return 2;
    }
    if (same_file(output.input, output.output)) {
        report("input and output are the same file");
        return 2;
    }

    std::filesystem::path final_index = output.output;
    final_index += ".csi";
    if (same_file(output.input, final_index)) {
        report("input is the output index path");
        return 2;
    }

    std::error_code filesystem_error;
    const bool output_exists =
        std::filesystem::exists(output.output, filesystem_error);
    if (filesystem_error) {
        report("cannot inspect the output path: " + filesystem_error.message());
        return 1;
    }
    if (output_exists && !output.force) {
        report("output already exists; use --force to overwrite it");
        return 2;
    }
    if (output_exists &&
        !std::filesystem::is_regular_file(output.output, filesystem_error)) {
        report("output is not a regular file");
        return 1;
    }

    filesystem_error.clear();
    const auto index_status =
        std::filesystem::symlink_status(final_index, filesystem_error);
    bool index_exists = false;
    if (filesystem_error == std::errc::no_such_file_or_directory) {
        filesystem_error.clear();
    } else if (filesystem_error) {
        report(
            "cannot inspect the output index path: " +
            filesystem_error.message()
        );
        return 1;
    } else {
        index_exists = std::filesystem::exists(index_status);
    }
    if (index_exists && !std::filesystem::is_symlink(index_status)) {
        filesystem_error.clear();
        if (!std::filesystem::is_regular_file(final_index, filesystem_error)) {
            report("output index path is not a regular file");
            return 1;
        }
        if (filesystem_error) {
            report(
                "cannot inspect the output index path: " +
                filesystem_error.message()
            );
            return 1;
        }
    }

    SamFilePtr input(sam_open(output.input.string().c_str(), "r"));
    if (!input) {
        report("cannot open '" + output.input.string() + "'");
        return 1;
    }
    const htsFormat *format = hts_get_format(input.get());
    if (format == nullptr || format->format != bam) {
        report("input is not a BAM file");
        return 1;
    }
    if (options.threads > 1 &&
        hts_set_threads(input.get(), options.threads) != 0) {
        report("cannot set input threads");
        return 1;
    }
    std::string filter_error;
    if (!apply_filter_expression(input.get(), options.filter, filter_error)) {
        report(filter_error);
        return 2;
    }

    HeaderPtr header(sam_hdr_read(input.get()));
    if (!header) {
        report("cannot read the header of '" + output.input.string() + "'");
        return 1;
    }
    if (options.drop_rg && count_distinct_samples(header.get()) > 1) {
        report("--drop-rg would merge multiple samples; refusing to continue");
        return 2;
    }

    HeaderPtr slim_header = build_slim_header(header.get());
    if (!slim_header) {
        report("'" + output.input.string() + "' has no usable @SQ lines");
        return 1;
    }

    const bool downsampling = options.downsample.enabled();
    const bool per_sample =
        options.downsample.mode == DownsampleMode::Sample;
    SampleGroups sample_groups;
    std::vector<std::int64_t> remaining;
    std::vector<std::int64_t> needed;
    std::int64_t input_reads = 0;
    std::mt19937_64 rng(options.downsample.seed);
    if (downsampling) {
        if (per_sample) {
            sample_groups = make_sample_groups(header.get());
        }
        std::string count_error;
        if (!count_reads_per_group(
                output.input,
                sample_groups,
                per_sample,
                options.filter,
                options.threads,
                remaining,
                count_error
            )) {
            report(count_error);
            return 1;
        }
        needed.resize(remaining.size());
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            needed[index] =
                target_for_group(options.downsample, remaining[index]);
            input_reads += remaining[index];
        }
    }

    TempFiles temporary;
    temporary.bam = temporary_path_for(output.output);
    if (temporary.bam.empty()) {
        report("cannot create a temporary output path");
        return 1;
    }
    remove_file(temporary.bam);

    SamFilePtr destination(sam_open(temporary.bam.string().c_str(), "wb"));
    if (!destination) {
        report("cannot create '" + temporary.bam.string() + "'");
        return 1;
    }
    if (options.threads > 1 &&
        hts_set_threads(destination.get(), options.threads) != 0) {
        report("cannot set output threads");
        return 1;
    }
    if (hts_set_opt(
            destination.get(), HTS_OPT_COMPRESSION_LEVEL, options.compression
        ) < 0) {
        report("cannot set compression level");
        return 1;
    }
    if (sam_hdr_write(destination.get(), slim_header.get()) < 0) {
        report("cannot write the output header");
        return 1;
    }

    BamPtr record(bam_init1());
    BamPtr slim_record(bam_init1());
    if (!record || !slim_record) {
        report("cannot allocate a BAM record");
        return 1;
    }

    const RecordPolicy policy{
        options.drop_seq,
        options.keep_qual,
        options.keep_name,
        options.drop_rg,
    };

    std::int64_t reads = 0;
    std::int32_t last_tid = -2;
    std::int64_t last_position = -1;
    std::string read_group;
    while (true) {
        if (interrupted()) {
            report("interrupted");
            return 1;
        }

        const int read_status =
            sam_read1(input.get(), header.get(), record.get());
        if (read_status == -1) {
            break;
        }
        if (read_status < -1) {
            if (options.filter.expression.empty()) {
                report("error reading '" + output.input.string() + "'");
            } else {
                report(
                    "error evaluating --filter-expr '" +
                    options.filter.expression + "' on '" +
                    output.input.string() + "'"
                );
            }
            return 1;
        }

        std::string error;
        if (!check_coordinate_order(
                record.get(), last_tid, last_position, error
            )) {
            report(error);
            return 1;
        }
        if (!record_passes_filter(record.get(), options.filter)) {
            continue;
        }

        bool keep = true;
        if (downsampling) {
            const int group = group_for_record(
                record.get(), per_sample, sample_groups, read_group
            );
            if (group < 0 ||
                static_cast<std::size_t>(group) >= remaining.size()) {
                report("internal sample grouping error");
                return 1;
            }
            auto &left = remaining[static_cast<std::size_t>(group)];
            auto &want = needed[static_cast<std::size_t>(group)];
            if (left <= 0 || want > left) {
                report("input changed while downsampling");
                return 1;
            }

            // Sequential sampling without replacement: keep the current
            // record with probability want / left. This selects exactly
            // `want` records from the remaining `left` uniformly.
            keep = false;
            if (want > 0) {
                if (left <= want) {
                    keep = true;
                } else {
                    keep =
                        random_below(rng, static_cast<std::uint64_t>(left)) <
                        static_cast<std::uint64_t>(want);
                }
                if (keep) {
                    --want;
                }
            }
            --left;
        }

        if (!transform_record(
                record.get(), slim_record.get(), policy, error
            )) {
            report(error);
            return 1;
        }
        if (!keep) {
            continue;
        }
        if (sam_write1(
                destination.get(), slim_header.get(), slim_record.get()
            ) < 0) {
            report("cannot write a BAM record");
            return 1;
        }
        ++reads;
    }

    if (downsampling) {
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            if (remaining[index] != 0 || needed[index] != 0) {
                report("input changed while downsampling");
                return 1;
            }
        }
    }

    samFile *raw_destination = destination.release();
    if (sam_close(raw_destination) < 0) {
        report("cannot finish '" + temporary.bam.string() + "'");
        return 1;
    }

    if (options.index) {
        temporary.index = temporary.bam;
        temporary.index += ".csi";
        remove_file(temporary.index);
        if (sam_index_build3(
                temporary.bam.string().c_str(),
                nullptr,
                14,
                options.threads
            ) < 0) {
            report("cannot index '" + temporary.bam.string() + "'");
            return 1;
        }
    }

    if (options.verify) {
        std::string error;
        if (!verify_bam(temporary.bam, reads, options.index, error)) {
            report("verification failed: " + error);
            return 3;
        }
    }

    if (!output.force) {
        filesystem_error.clear();
        if (std::filesystem::exists(output.output, filesystem_error)) {
            report("output appeared while writing; refusing to overwrite it");
            return 2;
        }
        if (filesystem_error) {
            report(
                "cannot inspect the output path: " +
                filesystem_error.message()
            );
            return 1;
        }
    }

    std::string error;
    std::filesystem::path index_backup;
    bool have_index_backup = false;
    if (index_exists) {
        index_backup = temporary_path_for(final_index);
        if (index_backup.empty()) {
            report("cannot create a temporary index path");
            return 1;
        }
        if (!replace_file(final_index, index_backup, error)) {
            report("cannot move the existing index aside: " + error);
            return 1;
        }
        have_index_backup = true;
    }

    auto restore_index = [&]() {
        if (!have_index_backup) {
            return;
        }
        std::string restore_error;
        if (!replace_file(index_backup, final_index, restore_error)) {
            report("cannot restore the existing index: " + restore_error);
            return;
        }
        have_index_backup = false;
    };

    if (options.index) {
        if (!replace_file(temporary.index, final_index, error)) {
            report(error);
            restore_index();
            return 1;
        }
        temporary.release_index();
    }

    if (!replace_file(temporary.bam, output.output, error)) {
        report(error);
        if (options.index && !remove_file(final_index)) {
            report("cannot remove the new index after a failed replacement");
        }
        restore_index();
        return 1;
    }
    temporary.release_bam();

    if (have_index_backup) {
        if (!remove_file(index_backup)) {
            report("cannot remove the temporary index backup");
        }
        have_index_backup = false;
    }

    if (stats != nullptr) {
        stats->reads = reads;
        stats->input_reads = downsampling ? input_reads : reads;
        stats->input_bytes = ::husk::file_size(output.input);
        stats->output_bytes = ::husk::file_size(output.output);
    }
    return 0;
}

bool verify_bam(
    const std::filesystem::path &path,
    std::int64_t expected_reads,
    bool expect_index,
    std::string &error
) {
    SamFilePtr file(sam_open(path.string().c_str(), "r"));
    if (!file) {
        error = "cannot reopen the output";
        return false;
    }
    const htsFormat *format = hts_get_format(file.get());
    if (format == nullptr || format->format != bam) {
        error = "output is not a BAM file";
        return false;
    }

    HeaderPtr header(sam_hdr_read(file.get()));
    if (!header) {
        error = "cannot read the output header";
        return false;
    }

    BamPtr record(bam_init1());
    if (!record) {
        error = "cannot allocate a BAM record";
        return false;
    }

    std::int64_t reads = 0;
    while (true) {
        const int status = sam_read1(file.get(), header.get(), record.get());
        if (status == -1) {
            break;
        }
        if (status < -1) {
            error = "output contains a truncated or corrupt record";
            return false;
        }
        ++reads;
    }
    if (reads != expected_reads) {
        error = "output read count " + std::to_string(reads) +
                " does not match " + std::to_string(expected_reads);
        return false;
    }

    if (expect_index) {
        hts_idx_t *index = sam_index_load(file.get(), path.string().c_str());
        if (index == nullptr) {
            error = "output index is missing or invalid";
            return false;
        }
        hts_idx_destroy(index);
    }
    return true;
}

}  // namespace husk
