#include "header.hpp"

#include "version.hpp"

#include <set>
#include <string>
#include <vector>

namespace husk {
namespace {

std::vector<std::string> split_tabs(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = line.find('\t', start);
        if (end == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            break;
        }
        fields.emplace_back(line.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

std::string_view strip_carriage_return(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

bool is_kept_header_line(std::string_view line) {
    line = strip_carriage_return(line);
    if (line.size() < 3 || line[0] != '@') {
        return false;
    }
    const std::string_view tag = line.substr(0, 3);
    if (tag != "@HD" && tag != "@SQ" && tag != "@RG") {
        return false;
    }
    return line.size() == 3 || line[3] == '\t';
}

std::string slim_header_text(std::string_view text) {
    std::string output;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            end == std::string_view::npos
                ? text.substr(start)
                : text.substr(start, end - start);
        if (is_kept_header_line(line)) {
            output.append(line.data(), line.size());
            output.push_back('\n');
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return output;
}

std::string husk_marker_line() {
    return std::string("@CO\tHUSK:1\tversion=") + HUSK_VERSION + "\n";
}

HeaderPtr build_slim_header(const sam_hdr_t *header) {
    if (header == nullptr) {
        return nullptr;
    }
    const char *text = sam_hdr_str(const_cast<sam_hdr_t *>(header));
    if (text == nullptr) {
        return nullptr;
    }
    std::string slim = slim_header_text(text);
    if (slim.empty()) {
        return nullptr;
    }
    slim += husk_marker_line();
    HeaderPtr result(sam_hdr_parse(slim.size(), slim.c_str()));
    if (!result || sam_hdr_nref(result.get()) < 1) {
        return nullptr;
    }
    return result;
}

std::unordered_map<std::string, std::string> read_group_samples(
    const sam_hdr_t *header
) {
    std::unordered_map<std::string, std::string> samples;
    if (header == nullptr) {
        return samples;
    }
    const char *text = sam_hdr_str(const_cast<sam_hdr_t *>(header));
    if (text == nullptr) {
        return samples;
    }

    std::string_view remaining(text);
    while (!remaining.empty()) {
        const std::size_t end = remaining.find('\n');
        const std::string_view raw =
            end == std::string_view::npos ? remaining : remaining.substr(0, end);
        const std::string_view line = strip_carriage_return(raw);
        if (line.rfind("@RG\t", 0) == 0) {
            std::string id;
            std::string sample;
            for (const std::string &field : split_tabs(line)) {
                if (field.rfind("ID:", 0) == 0) {
                    id = field.substr(3);
                } else if (field.rfind("SM:", 0) == 0) {
                    sample = field.substr(3);
                }
            }
            if (!id.empty() && !sample.empty()) {
                samples[id] = sample;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(end + 1);
    }
    return samples;
}

int count_distinct_samples(const sam_hdr_t *header) {
    if (header == nullptr) {
        return 0;
    }
    const char *text = sam_hdr_str(const_cast<sam_hdr_t *>(header));
    if (text == nullptr) {
        return 0;
    }

    std::set<std::string> samples;
    int unknown_samples = 0;
    std::string_view remaining(text);
    while (!remaining.empty()) {
        const std::size_t end = remaining.find('\n');
        const std::string_view raw =
            end == std::string_view::npos ? remaining : remaining.substr(0, end);
        const std::string_view line = strip_carriage_return(raw);
        if (line.rfind("@RG\t", 0) == 0) {
            bool has_sample = false;
            for (const std::string &field : split_tabs(line)) {
                if (field.rfind("SM:", 0) == 0 && field.size() > 3) {
                    samples.insert(field.substr(3));
                    has_sample = true;
                }
            }
            if (!has_sample) {
                // A read group without SM is an unknown sample. Treat each one
                // as distinct so --drop-rg cannot merge it silently.
                ++unknown_samples;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(end + 1);
    }

    return static_cast<int>(samples.size() + unknown_samples);
}

}  // namespace husk
