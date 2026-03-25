#pragma once

#include <cstdint>

#include "boost/container_hash/hash.hpp"

namespace ercat {

typedef int32_t RelNameId;
typedef int32_t RelId;
typedef int32_t EntId;
typedef int32_t VersionId;
typedef int64_t ObjId;
typedef int64_t EpochId;
typedef int64_t TxnId;

struct FileListId {
    FileListId() { }
    FileListId(EntId ent_id, ObjId obj_id) : ent_id_(ent_id), obj_id_(obj_id) { }

    bool operator==(const FileListId& other) const {
        return ent_id_ == other.ent_id_ && obj_id_ == other.obj_id_;
    }

    bool operator<(const FileListId& other) const {
        return (ent_id_ < other.ent_id_) || (ent_id_ == other.ent_id_ && obj_id_ < other.obj_id_);
    }

    bool operator!=(const FileListId& other) const {
        return !(*this == other); 
    }

    bool operator>(const FileListId& other) const {
        return other < *this; 
    }

    bool operator<=(const FileListId& other) const {
        return !(*this > other); 
    }

    bool operator>=(const FileListId& other) const {
        return !(*this < other);
    }

    EntId ent_id_;
    ObjId obj_id_;
};

inline std::size_t hash_value(FileListId const& file_list_id) {
    std::size_t seed = 0;
    boost::hash_combine(seed, file_list_id.ent_id_);
    boost::hash_combine(seed, file_list_id.obj_id_);
    return seed;
}

}