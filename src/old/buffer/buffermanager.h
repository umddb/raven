#pragma once

#include <vector>

#include "buffer/buffer.h"
#include "buffer/buffertable.h"
#include "storage/storagemanager.h"
#include "utils/paddedmutex.h"

namespace ercat {

class BufferManager {
public:    
    static void init();
    static BufferManager& getInstance();
    // way to quickly check if the read operation is complete
    static inline bool checkReadBuffersOperation(const ReadBuffersOperation* read_buf_op);
    static inline size_t hashBufTag(int fd, BlockNumber block_num);
    static inline size_t mapPartition(size_t hash_val);
    static inline void bufferStageCommon(IOHandle* ioh);
    static inline void bufferReadvCompleteOne(IOHandle* ioh);

    ~BufferManager();
    // synchronous call that calls both startRead() and waitRead()
    Buffer readBuffer(int fd, BlockNumber block_num, BufferStrategy buf_strat);
    bool startReadBuffer(ReadBuffersOperation* read_buf_op);
    void waitReadBuffers(ReadBuffersOperation* read_buf_op);
    void releaseBuffer(Buffer buffer);
    void markBufferDirty(Buffer buffer);
    void* getBufferBlock(Buffer buffer);

private:
    static BufferManager& getInstanceImpl(); 
    static bool pinBuffer(BufferDesc* buf_desc);
    static void unpinBuffer(BufferDesc* buf_desc); 

    BufferManager();
    BufferDesc* bufferAlloc(ReadBuffersOperation* read_buf_op, bool* found);
    Buffer getVictimBuffer(BufferStrategy buf_strat);
    bool asyncReadBuffers(ReadBuffersOperation* read_buf_op);
    bool readBuffersCanStartIO(Buffer buffer, bool no_wait);
    bool startBufferIO(BufferDesc* buf_desc, bool read, bool no_wait);
    void waitIO(BufferDesc* buf_desc);
    inline PaddedSharedMutex& mapPartitionLock(size_t hash_val);
    bool invalidateVictimBuffer(BufferDesc* buf_desc);
    void flushBuffer(BufferDesc* buf_desc);
    
    // buffer table
    BufferTable buffer_table_;
    // lock for each buffer partition
    std::vector<PaddedSharedMutex> buffer_partition_locks_;
    // Buffer Descriptors
    std::vector<BufferDesc> buffer_descs_;
    // Buffer blocks
    void* buffer_blocks_;
    // underlying storage manager
    StorageManager& str_mgr_;
};

}