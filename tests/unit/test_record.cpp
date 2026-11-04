#include "bam_slim.hpp"
#include "test_harness.hpp"
#include "test_helpers.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {

const char *kHeader =
    "@HD\tVN:1.6\tSO:coordinate\n"
    "@SQ\tSN:chr1\tLN:1000\n"
    "@RG\tID:s1\tSM:sample1\n";

std::string sequence_of(const bam1_t *record) {
    std::string sequence;
    sequence.resize(static_cast<std::size_t>(record->core.l_qseq));
    const std::uint8_t *packed = bam_get_seq(record);
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        sequence[index] = seq_nt16_str[bam_seqi(packed, index)];
    }
    return sequence;
}

std::string qualities_of(const bam1_t *record) {
    std::string qualities;
    qualities.resize(static_cast<std::size_t>(record->core.l_qseq));
    const std::uint8_t *raw = bam_get_qual(record);
    for (std::size_t index = 0; index < qualities.size(); ++index) {
        qualities[index] = static_cast<char>(raw[index] + 33);
    }
    return qualities;
}

}  // namespace

void test_record() {
    husk::HeaderPtr header = husk_test::make_header(kHeader);
    CHECK(header != nullptr);

    husk_test::BamPtr source = husk_test::make_record(
        header.get(),
        "r1\t0\tchr1\t1\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1\tNM:i:1"
    );

    {
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        const husk::RecordPolicy policy{};
        CHECK(husk::transform_record(
            source.get(), destination.get(), policy, error
        ));
        CHECK(error.empty());
        CHECK_EQ(
            std::string(bam_get_qname(destination.get())), std::string("*")
        );
        CHECK_EQ(destination->core.flag, source->core.flag);
        CHECK_EQ(destination->core.tid, source->core.tid);
        CHECK_EQ(destination->core.pos, source->core.pos);
        CHECK_EQ(destination->core.qual, source->core.qual);
        CHECK_EQ(destination->core.n_cigar, source->core.n_cigar);
        CHECK_EQ(
            bam_get_cigar(destination.get())[0],
            bam_get_cigar(source.get())[0]
        );
        CHECK_EQ(destination->core.l_qseq, 4);
        CHECK_EQ(sequence_of(destination.get()), std::string("ACGT"));
        CHECK_EQ(static_cast<int>(bam_get_qual(destination.get())[0]), 0xff);

        const std::uint8_t *rg = bam_aux_get(destination.get(), "RG");
        CHECK(rg != nullptr);
        CHECK_EQ(std::string(bam_aux2Z(rg)), std::string("s1"));
        CHECK(bam_aux_get(destination.get(), "NM") == nullptr);
    }

    {
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        husk::RecordPolicy policy{};
        policy.keep_name = true;
        CHECK(husk::transform_record(
            source.get(), destination.get(), policy, error
        ));
        CHECK_EQ(
            std::string(bam_get_qname(destination.get())), std::string("r1")
        );
    }

    {
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        husk::RecordPolicy policy{};
        policy.keep_qual = true;
        CHECK(husk::transform_record(
            source.get(), destination.get(), policy, error
        ));
        CHECK_EQ(qualities_of(destination.get()), std::string("IIII"));
    }

    {
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        husk::RecordPolicy policy{};
        policy.drop_seq = true;
        CHECK(husk::transform_record(
            source.get(), destination.get(), policy, error
        ));
        CHECK_EQ(destination->core.l_qseq, 0);
        CHECK_EQ(static_cast<int>(destination->core.n_cigar), 1);
        CHECK_EQ(
            bam_get_cigar(destination.get())[0],
            bam_get_cigar(source.get())[0]
        );
        CHECK(bam_aux_get(destination.get(), "RG") != nullptr);
    }

    {
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        husk::RecordPolicy policy{};
        policy.drop_rg = true;
        CHECK(husk::transform_record(
            source.get(), destination.get(), policy, error
        ));
        CHECK(bam_aux_get(destination.get(), "RG") == nullptr);
    }

    {
        husk_test::BamPtr malformed = husk_test::make_record(
            header.get(),
            "r2\t0\tchr1\t2\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:i:1"
        );
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        CHECK(!husk::transform_record(
            malformed.get(), destination.get(), husk::RecordPolicy{}, error
        ));
        CHECK(error.find("RG") != std::string::npos);
    }

    {
        husk_test::BamPtr long_cigar = husk_test::make_record(
            header.get(),
            "r3\t0\tchr1\t3\t60\t2M2M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1"
        );
        std::vector<std::uint8_t> cg(5 + 3 * 4);
        cg[0] = 'I';
        const std::uint32_t count = 3;
        std::memcpy(cg.data() + 1, &count, sizeof(count));
        const std::uint32_t op = (4u << 4u) | 0u;
        for (int index = 0; index < 3; ++index) {
            std::memcpy(cg.data() + 5 + index * 4, &op, sizeof(op));
        }
        CHECK(bam_aux_append(
                  long_cigar.get(),
                  "CG",
                  'B',
                  cg.size(),
                  cg.data()
              ) >= 0);

        husk_test::BamPtr destination(bam_init1());
        std::string error;
        CHECK(husk::transform_record(
            long_cigar.get(), destination.get(), husk::RecordPolicy{}, error
        ));
        const std::uint8_t *out_cg =
            bam_aux_get(destination.get(), "CG");
        CHECK(out_cg != nullptr);
        CHECK_EQ(static_cast<int>(bam_auxB_len(out_cg)), 3);
        CHECK_EQ(static_cast<int>(destination->core.n_cigar), 2);
    }

    {
        husk_test::BamPtr not_long = husk_test::make_record(
            header.get(),
            "r4\t0\tchr1\t4\t60\t4M\t*\t0\t0\tACGT\tIIII\tRG:Z:s1"
        );
        std::vector<std::uint8_t> cg(5 + 1 * 4, 0);
        cg[0] = 'I';
        const std::uint32_t count = 1;
        std::memcpy(cg.data() + 1, &count, sizeof(count));
        CHECK(bam_aux_append(
                  not_long.get(), "CG", 'B', cg.size(), cg.data()
              ) >= 0);
        husk_test::BamPtr destination(bam_init1());
        std::string error;
        CHECK(husk::transform_record(
            not_long.get(), destination.get(), husk::RecordPolicy{}, error
        ));
        CHECK(bam_aux_get(destination.get(), "CG") == nullptr);
    }
}
