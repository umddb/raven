#pragma once

#include <string>

namespace ercat {

// Class for storing catalog objects, consisting of predefined set of hash maps.
// The object is initialized at the very beginning from the underlying Postgres instance
class StringUtil {
public:

struct Match { size_t pos; int pattern_id; };

static std::string replaceTwoPatterns(std::string_view input,
        std::string_view before1, std::string_view after1,
        std::string_view before2, std::string_view after2, size_t start_idx);
};

}