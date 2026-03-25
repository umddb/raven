#include "gc/versionchain.h"

namespace ercat {

VersionChain::VersionChain() : tail_(nullptr) {
    // dummy head node with vid of 0 
    insert(0);
}

VersionChain::~VersionChain() {
    VersionNode * cur_node = tail_;
    // iterate the linked list in reverse order to delete all the nodes
    while (cur_node != nullptr) {
        VersionNode* next_node = cur_node;
        cur_node = cur_node->prev_;
        delete next_node;
    }
}

bool VersionChain::empty() {
    return (tail_ == nullptr);
}

bool VersionChain::insert(VersionId vid) {
    if (version_idx_.contains(vid)) {
        return false;
    }

    VersionNode* v_node = new VersionNode();
    v_node->val_ = vid;
    version_idx_.emplace(vid, v_node);

    VersionNode* prev_node = tail_;
    VersionNode* next_node = nullptr;
    // iterate in reverse order to search for previous version
    while (prev_node != nullptr && prev_node->val_ > vid) {
        next_node = prev_node; 
        prev_node = prev_node->prev_;
    }

    if (prev_node != nullptr) {
        v_node->prev_ = prev_node;
        prev_node->next_ = v_node;
    }

    if (next_node != nullptr) {
        v_node->next_ = next_node;
        next_node->prev_ = v_node;
    }

    if (prev_node == tail_) {
        tail_ = v_node; 
    }

    return true;
}

std::pair<VersionId, VersionId> VersionChain::remove(VersionId vid) {
    std::pair<VersionId, VersionId> result(invalid_vid_, invalid_vid_);
    // the vid does not exist
    if (!version_idx_.contains(vid)) {
        return result;
    }
    
    result.first = vid - 1;
    result.second = vid + 1;
    VersionNode* v_node = version_idx_.at(vid);
    VersionNode* prev = v_node->prev_;
    VersionNode* next = v_node->next_;

    if (prev != nullptr) {
        prev->next_ = next;
        result.first = prev->val_;
    }

    if (next != nullptr) {
        next->prev_ = prev;
        result.second = next->val_;
    }

    if (v_node == tail_) {
        tail_ = prev;
    }

    version_idx_.erase(vid);
    delete v_node;

    return result;
}


}