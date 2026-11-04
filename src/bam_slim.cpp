#include "bam_slim.hpp"

#include "header.hpp"
#include "platform.hpp"

#include <htslib/hts.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

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

int aux_type_size(char type) {
    switch (type) {
    case 'A':
    case 'c':
    case 'C':
        return 1;
    case 's':
    case 'S':
        return 2;
    case 'i':
    case 'I':
    case 'f':
        return 4;
    case 'd':
        return 8;
    default:
        return 0;
    }
}

bool append_aux(
    bam1_t *destination,
    const bam1_t *source,
    const char *tag,
    bool require_string,
    std::string &error
) {
    const std::uint8_t *aux = bam_aux_get(source, tag);
    if (aux == nullptr) {
        return true;
    }

    const char type = static_cast<char>(aux[0]);
    if (require_string && type != 'Z') {
        error = std::string("read has a malformed ") + tag + " tag";
        return false;
    }

    if (type == 'B') {
        const std::int32_t count = bam_auxB_len(aux);
        const int subtype_size = aux_type_size(static_cast<char>(aux[1]));
        if (count < 0 || subtype_size <= 0) {
            error = std::string("read has a malformed ") + tag + " tag";
            return false;
        }
        const std::size_t data_length =
            5 + static_cast<std::size_t>(count) *
                    static_cast<std::size_t>(subtype_size);
        if (bam_aux_append(
                destination, tag, 'B', data_length, aux + 1
            ) < 0) {
            error = std::string("cannot copy the ") + tag + " tag";
            return false;
        }
        return true;
    }

    if (type == 'Z' || type == 'H') {
        const char *value = bam_aux2Z(aux);
        if (value == nullptr) {
            error = std::string("read has a malformed ") + tag + " tag";
            return false;
        }
        const std::size_t data_length = std::strlen(value) + 1;
        if (bam_aux_append(
                destination,
                tag,
                type,
                data_length,
                reinterpret_cast<const std::uint8_t *>(value)
            ) < 0) {
            error = std::string("cannot copy the ") + tag + " tag";
            return false;
        }
        return true;
    }

    const int size = aux_type_size(type);
    if (size <= 0) {
        error = std::string("read has a malformed ") + tag + " tag";
        return false;
    }
    if (bam_aux_append(
            destination, tag, type, static_cast<std::size_t>(size), aux + 1
        ) < 0) {
        error = std::string("cannot copy the ") + tag + " tag";
        return false;
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

    const char *name = policy.keep_name ? bam_get_qname(source) : nullptr;
    const std::size_t name_length =
        policy.keep_name ? std::strlen(name) : 0;

    std::string sequence;
    std::string qualities;
    const char *sequence_ptr = nullptr;
    const char *quality_ptr = nullptr;
    std::size_t sequence_length = 0;

    if (!policy.drop_seq && source->core.l_qseq > 0) {
        sequence_length = static_cast<std::size_t>(source->core.l_qseq);
        sequence.resize(sequence_length);
        const std::uint8_t *packed = bam_get_seq(source);
        for (std::size_t index = 0; index < sequence_length; ++index) {
            sequence[index] = seq_nt16_str[bam_seqi(packed, index)];
        }
        sequence_ptr = sequence.data();

        if (policy.keep_qual) {
            const std::uint8_t *raw = bam_get_qual(source);
            if (raw[0] != 0xff) {
                qualities.assign(
                    reinterpret_cast<const char *>(raw), sequence_length
                );
                quality_ptr = qualities.data();
            }
        }
    }

    const std::uint32_t *cigar =
        source->core.n_cigar > 0 ? bam_get_cigar(source) : nullptr;
    if (bam_set1(
            destination,
            name_length,
            name,
            source->core.flag,
            source->core.tid,
            source->core.pos,
            source->core.qual,
            static_cast<std::size_t>(source->core.n_cigar),
            cigar,
            source->core.mtid,
            source->core.mpos,
            source->core.isize,
            sequence_length,
            sequence_ptr,
            quality_ptr,
            0
        ) < 0) {
        error = "cannot rebuild a BAM record";
        return false;
    }

    // A CIGAR longer than 65535 operations is represented by a two-operation
    // placeholder plus a CG:B:I tag. Preserve CG or the real CIGAR is lost.
    if (source->core.n_cigar == 2 && bam_aux_get(source, "CG") != nullptr) {
        if (!append_aux(destination, source, "CG", false, error)) {
            return false;
        }
    }
    if (!policy.drop_rg && !append_aux(destination, source, "RG", true, error)) {
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
    if (same_file(output.input, output.output)) {
        report("input and output are the same file");
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
        report("'" + output.input.string() + "' has no usable header lines");
        return 1;
    }

    TempFiles temporary;
    temporary.bam = temporary_path_for(output.output);
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
            report("error reading '" + output.input.string() + "'");
            return 1;
        }

        std::string error;
        if (!check_coordinate_order(
                record.get(), last_tid, last_position, error
            )) {
            report(error);
            return 1;
        }
        if (!transform_record(
                record.get(), slim_record.get(), policy, error
            )) {
            report(error);
            return 1;
        }
        if (sam_write1(
                destination.get(), slim_header.get(), slim_record.get()
            ) < 0) {
            report("cannot write a BAM record");
            return 1;
        }
        ++reads;
    }

    samFile *raw_destination = destination.release();
    if (sam_close(raw_destination) < 0) {
        report("cannot finish '" + temporary.bam.string() + "'");
        return 1;
    }

    if (options.index) {
        temporary.index = temporary.bam;
        temporary.index += ".csi";
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
    }

    std::string error;
    if (!replace_file(temporary.bam, output.output, error)) {
        report(error);
        return 1;
    }
    temporary.release_bam();

    if (options.index) {
        std::filesystem::path final_index = output.output;
        final_index += ".csi";
        if (!replace_file(temporary.index, final_index, error)) {
            remove_file(final_index);
            report(error);
            return 1;
        }
        temporary.release_index();
    } else {
        std::filesystem::path stale_index = output.output;
        stale_index += ".csi";
        remove_file(stale_index);
    }

    if (stats != nullptr) {
        stats->reads = reads;
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
