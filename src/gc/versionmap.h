#pragma once

#include <utility>

#include "boost/unordered/concurrent_flat_map.hpp"

#include "gc/gcgraph.h"
#include "gc/versionchain.h"
#include "utils/paddedmutex.h"
#include "utils/types.h"

namespace ercat {

// Data structure for keeping track of version chains for filelist objects
class VersionMap {
public:
    VersionMap();
    ~VersionMap();
    bool insert(ExtVertex ext_vertex);
    std::pair<ExtVertex, ExtVertex> remove(ExtVertex ext_vertex);

private:
    PaddedMutex* mtx_array_;
    // maps file list id to corresponding version chain
    boost::concurrent_flat_map<FileListId, VersionChain*> version_map_;

};

}