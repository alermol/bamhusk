#include "bam_slim.hpp"
#include "platform.hpp"

#include <htslib/hts.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path work_directory() {
    static const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("husk-fuzz-" + std::to_string(husk::process_id()));
    std::filesystem::create_directories(directory);
    return directory;
}

class StderrSilencer {
  public:
    StderrSilencer() {
#ifdef _WIN32
        saved_ = _dup(_fileno(stderr));
        if (std::freopen("NUL", "w", stderr) == nullptr) {
            saved_ = -1;
        }
#else
        saved_ = ::dup(fileno(stderr));
        if (std::freopen("/dev/null", "w", stderr) == nullptr) {
            saved_ = -1;
        }
#endif
    }

    ~StderrSilencer() {
        if (saved_ < 0) {
            return;
        }
        std::fflush(stderr);
#ifdef _WIN32
        _dup2(saved_, _fileno(stderr));
        _close(saved_);
#else
        ::dup2(saved_, fileno(stderr));
        ::close(saved_);
#endif
    }

    StderrSilencer(const StderrSilencer &) = delete;
    StderrSilencer &operator=(const StderrSilencer &) = delete;

  private:
    int saved_ = -1;
};

}  // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    hts_set_log_level(HTS_LOG_OFF);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const std::filesystem::path directory = work_directory();
    const std::filesystem::path input = directory / "input.bam";
    const std::filesystem::path output = directory / "output.bam";

    {
        std::ofstream stream(input, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return 0;
        }
        stream.write(
            reinterpret_cast<const char *>(data),
            static_cast<std::streamsize>(size)
        );
    }

    std::filesystem::remove(output);
    std::filesystem::remove(std::filesystem::path(output.string() + ".csi"));

    {
        StderrSilencer silence;
        husk::OutputOptions output_options;
        output_options.force = true;
        output_options.input = input;
        output_options.output = output;

        husk::SlimOptions slim_options;
        slim_options.threads = 1;
        (void)husk::write_slim_bam(output_options, slim_options, nullptr);
    }

    std::filesystem::remove(output);
    std::filesystem::remove(std::filesystem::path(output.string() + ".csi"));
    return 0;
}
