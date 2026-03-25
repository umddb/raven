#pragma once

#include <iostream>
#include <vector>

#include <utils/spinlock.h>

namespace ercat {

template<typename T>
class alignas(64) ConcurrentQueue {
public:
    struct Node {
        Node() : next_(nullptr) { }
        Node(const T& value) : value_(value), next_(nullptr) { }
        Node(const T& value, Node * next) : value_(value), next_(next) { }

        T value_;
        Node * next_;
    };

    ConcurrentQueue() {
        Node * new_node = new Node();
        head_ = tail_ = new_node;
    }

    // push: Add an element to the back of the queue 
    void push(T value) {
        Node * new_node = new Node(value);

        tail_lock_.lock();
        tail_->next_ = new_node;
        tail_ = new_node;
        tail_lock_.unlock();
    }

    // pop: Remove and return the element from the front of the queue
    bool pop(T& value) {
        Node * node; 
        Node * new_head;
        
        head_lock_.lock();
        node = head_;
        new_head = node->next_;
        if (new_head == nullptr) {
            head_lock_.unlock();
            return false;
        }
        value = new_head->value_;
        head_ = new_head;
        head_lock_.unlock();
        
        delete node;

        return true;
    }

    // Get the front element without removing it
    bool front(T & t) {
        head_lock_.lock();
        Node * head = head_->next_;
        if (head == nullptr) {
            head_lock_.unlock();
            return false;
        }
        t = head->value_;
        head_lock_.unlock();
        return true;
    }

    bool empty() {
        head_lock_.lock();
        bool is_empty = (head_->next_ == nullptr);
        head_lock_.unlock();
        return is_empty;
    }

private:
    Node * head_;
    // to avoid false sharing
    char padding_[64 - sizeof(Node*)];
    Node * tail_;
    char padding2_[64 - sizeof(Node*)];
    SpinLock head_lock_;
    SpinLock tail_lock_;

};

}