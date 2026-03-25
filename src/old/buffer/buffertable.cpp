#include "buffer/buffertable.h"
#include "buffer/buffermanager.h"

namespace ercat {

BufferTable::BufferTable() : capacity_(0) { }

BufferTable::~BufferTable() {
    // delete all the hash chains in every bucket
    for (auto& bucket : buffer_table_) {
        HashEntry* cur,* next = bucket.next_;
        while (next != nullptr) {
            cur = next;
            next = cur->next_;
            delete cur;
        }
    }
}

void BufferTable::init(size_t capacity) {
    capacity_ = capacity;
    buffer_table_.resize(capacity_);
    free_lists_.resize(PARTITION_NUM);
    partition_size_ = capacity_ / PARTITION_NUM;
}

int BufferTable::get(int fd, BlockNumber block_num, bool* found) {
    size_t hash_val = BufferManager::hashBufTag(fd, block_num);
    return get(fd, block_num, hash_val, found);
}

int BufferTable::get(int fd, BlockNumber block_num, size_t hash_val, bool* found) {
    int id;
    size_t idx = hashToIdx(hash_val);
    
    HashEntry* cur = &buffer_table_[idx];
    bool local_found = false;
    while ((!local_found) & (cur != nullptr)) {
        local_found = (cur->fd_ == fd) & (cur->block_num_ == block_num);
        id = cur->id_;
        cur = cur->next_;
    }

    *found = local_found;
    return id;
}

// try insert and return existing ID if there is already an entry. Otherwise return -1
int BufferTable::insert(int fd, BlockNumber block_num, int id) {
    size_t hash_val = BufferManager::hashBufTag(fd, block_num);
    return insert(fd, block_num, hash_val, id);
}

int BufferTable::insert(int fd, BlockNumber block_num, size_t hash_val, int id) {
    int id;
    size_t idx = hashToIdx(hash_val);
    HashEntry* cur = &buffer_table_[idx];
    HashEntry* head = cur;
    // search the bucket chain
    bool found = false;
    while ((!found) & (cur != nullptr)) {
        found = (cur->fd_ == fd) & (cur->block_num_ == block_num);
        id = cur->id_;
        cur = cur->next_;
    }
    // entry already exists
    if (found) {
        return id;
    }

    HashEntry* entry;
    // if head entry is not valid (empty), replace it
    if (!head->valid()) {
        entry = head;
    }
    // otherwise allocate a new entry
    else {
        HashEntryList& free_list = free_lists_[BufferManager::mapPartition(hash_val)];
        // check the free list first
        if (!free_list.empty()) {
            entry = free_list.pop();
        }
        else {
            entry = new HashEntry();
        }
        // add the new entry after the head
        entry->next_ = head->next_;
        head->next_ = entry;
    }

    entry->fd_ = fd;
    entry->block_num_ = block_num;
    entry->id_ = id;

    return -1;
}

bool BufferTable::remove(int fd, BlockNumber block_num) {
    size_t hash_val = BufferManager::hashBufTag(fd, block_num);
    return remove(fd, block_num, hash_val);
}

bool BufferTable::remove(int fd, BlockNumber block_num, size_t hash_val) {
    size_t idx = hashToIdx(hash_val);
    HashEntry* cur = &buffer_table_[idx];
    HashEntry* prev = cur;

    // search the bucket chain
    bool found = false;
    while (cur != nullptr) {
        found = (cur->fd_ == fd) & (cur->block_num_ == block_num);
        if (found) {
            break;
        }
        prev = cur;
        cur = cur->next_;
    }

    // entry does not exist
    if (!found) {
        return false;
    }

    // entry is not in place
    if (cur != &buffer_table_[idx]) {
        prev->next_ = cur->next_;
        // add the entry to free list
        HashEntryList& free_list = free_lists_[BufferManager::mapPartition(hash_val)];
        free_list.push(cur);
    }
    // entry is in place
    else {
        // invalidate, but leave the chain intact
        cur->fd_ = -1;
    }

    return true;
}

size_t BufferTable::hashToIdx(size_t hash_val) {
    // tricky because the hash table is partitioend by PARTITION_NUM. Hope this works...
    return (partition_size_ * BufferManager::mapPartition(hash_val)) + ((hash_val / PARTITION_NUM) % partition_size_);
}

}
