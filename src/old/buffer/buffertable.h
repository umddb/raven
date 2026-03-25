#pragma once

#include <vector>
#include "buffer/buffer.h"

namespace ercat {

struct HashEntry {
    // -1 denotes that the hash entry is invalid
    HashEntry() : fd_(-1), next_(nullptr) { }
    void reset() { 
        fd_ = -1;
        next_ = nullptr;
    }
    bool valid() {
        return (fd_ != -1);
    }

    // key
    int fd_;
    BlockNumber block_num_;
    // value
    int id_;
    // private next field
    HashEntry* next_;
};

// simple linked list structure for free lists of HashEntry
class alignas(64) HashEntryList {
public:
    HashEntryList() : head_(nullptr), size_(0) { }
    
    // deallocate all the nodes in the list
    ~HashEntryList() {
        HashEntry* cur,* next = head_;
        while (next != nullptr) {
            cur = next;
            next = cur->next_;
            delete cur;
        }
    }

    bool empty() {
        return (size_ == 0);
    }

    size_t size() {
        return size_;
    }

    void push(HashEntry* entry) {
        entry->next_ = head_;
        head_ = entry;
        size_++;
    }    

    // precondition: the list is not empty!
    HashEntry* pop() {
        HashEntry* entry = head_;
        head_ = entry->next_;
        size_--;
        entry->reset();
        return entry;
    }

private:
    HashEntry* head_;
    size_t size_;

};

class BufferTable {
public:
    BufferTable();
    ~BufferTable();
    void init(size_t capacity);
    int get(int fd, BlockNumber block_num, bool* found);
    int get(int fd, BlockNumber block_num, size_t hash_val, bool* found);
    int insert(int fd, BlockNumber block_num, int id);
    int insert(int fd, BlockNumber block_num, size_t hash_val, int id);
    bool remove(int fd, BlockNumber block_num);
    bool remove(int fd, BlockNumber block_num, size_t hash_val);

private:
    inline size_t hashToIdx(size_t hash_val);

    std::vector<HashEntry> buffer_table_;
    // lists of free HashEntry, one per partition
    std::vector<HashEntryList> free_lists_;
    // must be power of 2
    size_t capacity_;
    // size of each partition
    size_t partition_size_;

};

}