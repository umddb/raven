#pragma once

namespace ercat {

struct DListNode {
    DListNode() : prev_(nullptr), next_(nullptr) { }
    ~DListNode() { }

    DListNode* prev_;
    DListNode* next_;
};    

// Circular doubly linked list
struct DList {
    DList() : count_(0) {
        head_.prev_ = &head_;
        head_.next_ = &head_;
    }

    ~DList() { }

    void pushFront(DListNode* node) {
        node->next_ = head_.next_;
	    node->prev_ = &head_;
	    node->next_->prev_ = node;
	    head_.next_ = node;
        count_++;
    }

    void pushBack(DListNode* node) {
        node->next_ = &head_;
	    node->prev_ = head_.prev_;
	    node->prev_->next_ = node;
	    head_.prev_ = node;
        count_++;
    }

    // precondition: list is not empty 
    DListNode* popFront() {
        DListNode* node = head_.next_;
	    remove(node);
        return node;
    }

    void remove(DListNode* node) {
        node->prev_->next_ = node->next_;
	    node->next_->prev_ = node->prev_;
        count_--;
    }

    DListNode* front() {
        return head_.next_;
    }

    uint32_t size() {
        return count_;
    }

    bool isEmpty() {
        return (count_ == 0);
    }

    DListNode head_;
    uint32_t count_;
};

}

