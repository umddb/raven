#include "gc/versionmap.h"

namespace ercat {

VersionMap::VersionMap() {
    mtx_array_ = new PaddedMutex[32];
}

VersionMap::~VersionMap() {
    version_map_.visit_all([&](auto& x){ delete x.second; });
    delete[] mtx_array_;
}

bool VersionMap::insert(ExtVertex ext_vertex) {
    FileListId file_list_id(ext_vertex.ent_id_, ext_vertex.obj_id_);
    
    std::size_t mtx_idx = hash_value(file_list_id) % 32;
    std::unique_lock<std::mutex> lock(mtx_array_[mtx_idx].mtx_);

    VersionChain* version_chain;
    bool found = version_map_.visit(file_list_id, [&](const auto& x){ version_chain = x.second; });
    if (!found) {
        version_chain = new VersionChain();
        version_map_.emplace(file_list_id, version_chain);
    }

    bool inserted = version_chain->insert(ext_vertex.vid_);

    lock.unlock();

    return inserted;
}

// TODO might want a special flag to delete next vid entry for the file list that no longer exists in the catalog.
std::pair<ExtVertex, ExtVertex> VersionMap::remove(ExtVertex ext_vertex) {
    FileListId file_list_id(ext_vertex.ent_id_, ext_vertex.obj_id_);
    std::pair<ExtVertex, ExtVertex> result(ext_vertex, ext_vertex);
    result.first.vid_ = VersionChain::invalid_vid_;
    result.second.vid_ = VersionChain::invalid_vid_;

    std::size_t mtx_idx = hash_value(file_list_id) % 32;
    std::unique_lock<std::mutex> lock(mtx_array_[mtx_idx].mtx_);

    VersionChain* version_chain;
    bool found = version_map_.visit(file_list_id, [&](const auto& x){ version_chain = x.second; });
    if (!found) {
        lock.unlock();
        return result;
    }

    std::pair<VersionId, VersionId> vid_pair = version_chain->remove(ext_vertex.vid_);
    if (version_chain->empty()) {
        version_map_.erase(file_list_id);
        delete(version_chain);
    }

    lock.unlock();

    result.first.vid_ = vid_pair.first;
    result.second.vid_ = vid_pair.second;
    return result;
}

}