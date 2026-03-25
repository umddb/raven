#include <vector>

#include "utils/stringutil.h"

namespace ercat {

std::string StringUtil::replaceTwoPatterns(std::string_view input,
        std::string_view before1, std::string_view after1,
        std::string_view before2, std::string_view after2, size_t start_idx) {

    if (input.empty()) return "";

    // Step 1: Calculate the exact output size to perform only 1 allocation
    size_t final_size = 0;
    
    std::vector<Match> matches;
    matches.reserve(16); // Small stack-like optimization or minor heap use

    for (size_t i = start_idx; i < input.size(); ) {
        // Check for longest pattern first to handle overlapping cases correctly
        // We assume f2 ("foo1") might be longer than f1 ("foo")
        bool m2 = (i + before2.size() <= input.size()) && (input.substr(i, before2.size()) == before2);
        if (m2) {
            matches.push_back({i, 2});
            final_size += after2.size();
            i += before2.size();
            continue;
        }

        bool m1 = (i + before1.size() <= input.size()) && (input.substr(i, before1.size()) == before1);
        if (m1) {
            matches.push_back({i, 1});
            final_size += after1.size();
            i += before1.size();
            continue;
        }

        final_size++;
        i++;
    }

    // Step 2: Build the string in the pre-allocated buffer
    std::string result;
    result.reserve(final_size);

    size_t last_pos = 0;
    for (const auto& match : matches) {
        // Append the skipped part (text between matches)
        result.append(input.substr(last_pos, match.pos - last_pos));
        
        // Append the replacement
        if (match.pattern_id == 1) {
            result.append(after1);
            last_pos = match.pos + before1.size();
        } else {
            result.append(after2);
            last_pos = match.pos + before2.size();
        }
    }

    // Append remaining characters after the last match
    if (last_pos < input.size()) {
        result.append(input.substr(last_pos));
    }

    return result;
    }

}