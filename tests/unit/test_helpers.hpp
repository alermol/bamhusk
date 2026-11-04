#pragma once

#include "header.hpp"
#include "platform.hpp"

#include <htslib/kstring.h>
#include <htslib/sam.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace husk_test {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("husk-test-" + std::to_string(husk::process_id()) + "-" +
                std::to_string(++counter));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct BamDeleter {
    void operator()(bam1_t *record) const {
        if (record != nullptr) {
            bam_destroy1(record);
        }
    }
};

struct SamFileDeleter {
    void operator()(samFile *file) const {
        if (file != nullptr) {
            sam_close(file);
        }
    }
};

using BamPtr = std::unique_ptr<bam1_t, BamDeleter>;
using SamFilePtr = std::unique_ptr<samFile, SamFileDeleter>;

inline husk::HeaderPtr make_header(const std::string &text) {
    return husk::HeaderPtr(sam_hdr_parse(text.size(), text.c_str()));
}

inline BamPtr make_record(sam_hdr_t *header, const std::string &sam_line) {
    BamPtr record(bam_init1());
    kstring_t line{0, 0, nullptr};
    if (kputs(sam_line.c_str(), &line) < 0) {
        std::free(line.s);
        throw std::runtime_error("cannot allocate SAM line");
    }
    const int status = sam_parse1(&line, header, record.get());
    std::free(line.s);
    if (status < 0) {
        throw std::runtime_error("cannot parse SAM line: " + sam_line);
    }
    return record;
}

inline void write_bam(
    const std::filesystem::path &path,
    const std::string &header_text,
    const std::vector<std::string> &records
) {
    husk::HeaderPtr header = make_header(header_text);
    if (!header) {
        throw std::runtime_error("cannot parse header");
    }
    SamFilePtr output(sam_open(path.string().c_str(), "wb"));
    if (!output) {
        throw std::runtime_error("cannot create BAM");
    }
    if (sam_hdr_write(output.get(), header.get()) < 0) {
        throw std::runtime_error("cannot write header");
    }
    for (const std::string &line : records) {
        BamPtr record = make_record(header.get(), line);
        if (sam_write1(output.get(), header.get(), record.get()) < 0) {
            throw std::runtime_error("cannot write record");
        }
    }
}

inline std::string read_header_text(const std::filesystem::path &path) {
    SamFilePtr input(sam_open(path.string().c_str(), "r"));
    if (!input) {
        throw std::runtime_error("cannot open BAM");
    }
    husk::HeaderPtr header(sam_hdr_read(input.get()));
    if (!header) {
        throw std::runtime_error("cannot read header");
    }
    return std::string(sam_hdr_str(header.get()));
}

}  // namespace husk_test
