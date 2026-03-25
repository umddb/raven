#pragma once

#include <limits>
#include <utility>

#include "boost/unordered/unordered_flat_map.hpp"

#include "utils/types.h"

namespace ercat {

class VersionChain {
public:
    static constexpr VersionId invalid_vid_ = -1;
    VersionChain();
    ~VersionChain();
    bool empty();
    bool insert(VersionId vid);
    std::pair<VersionId, VersionId> remove(VersionId vid);

private:
    struct VersionNode {
        VersionNode() : val_(0), prev_(nullptr), next_(nullptr) { }

        VersionId val_;
        VersionNode* prev_;
        VersionNode* next_;
    };

    // maps vid to the corresponding VersionNode
    boost::unordered_flat_map<VersionId, VersionNode*> version_idx_;
    // the linked list
    VersionNode* tail_;

};

}