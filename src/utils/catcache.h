#pragma once

#include "fmt/format.h"
#include <iterator>
#include <vector>

#include "boost/functional/hash_fwd.hpp"
#include "boost/unordered/unordered_flat_map.hpp"
#include "postgresql/libpq-fe.h"
#include "utils/config.h"
#include "utils/types.h"
#include "utils/paddedmutex.h"

namespace ercat {
    
struct EntityEntry {
    EntityEntry() { }
    EntityEntry(EntId ent_id, bool is_abstract, bool is_file_list, bool is_root) : 
            ent_id_(ent_id), is_abstract_(is_abstract), is_file_list_(is_file_list), is_root_(is_root) { }
    EntityEntry(const EntityEntry& other): ent_id_(other.ent_id_), is_abstract_(other.is_abstract_),
            is_file_list_(other.is_file_list_), is_root_(other.is_root_) { }

    EntId ent_id_;
    bool is_abstract_;
    bool is_file_list_;
    bool is_root_;
};

// Relation signature, which defines a relation. 
// A relation may have multiple relation signatures due to inheritance
struct RelSig {
    RelSig() { }
    RelSig(RelNameId rel_name_id, EntId src_ent_id, EntId dest_ent_id) : rel_name_id_(rel_name_id), src_ent_id_(src_ent_id),
        dest_ent_id_(dest_ent_id) { }
    std::string toString() {
        std::string str;
        fmt::format_to(
            std::back_inserter(str),
            "{}({},{})",
            rel_name_id_,
            src_ent_id_,
            dest_ent_id_
        );
        return str;
    }

    RelNameId rel_name_id_;
    EntId src_ent_id_;
    EntId dest_ent_id_;
};

// equality operator for RelSig
inline bool operator==(const RelSig& left, const RelSig& right) {
    return (left.rel_name_id_ == right.rel_name_id_) && (left.src_ent_id_ == right.src_ent_id_)
            && (left.dest_ent_id_ == right.dest_ent_id_);
}

// Boost hash function for RelSig
inline std::size_t hash_value(RelSig const& rel_sig)
{
    std::size_t seed = 0;
    boost::hash_combine(seed, rel_sig.rel_name_id_);
    boost::hash_combine(seed, rel_sig.src_ent_id_);
    boost::hash_combine(seed, rel_sig.dest_ent_id_);
    return seed;
}

// Class for storing catalog objects, consisting of predefined set of hash maps.
// The object is initialized at the very beginning from the underlying Postgres instance
class CatCache {
public:
    static bool init();
    static bool refresh();
    static const CatCache& getInstance();
    static std::shared_mutex& mtx();

    ~CatCache();
    // Access methods for getting underlying hash maps
    boost::unordered_flat_map<std::string, EntityEntry>& entityMap();
    boost::unordered_flat_map<EntId, bool>& fileLists();
    boost::unordered_flat_map<EntId, const std::vector<EntId>*>& ancestorMap();
    boost::unordered_flat_map<EntId, const std::vector<EntId>*>& descendantMap();
    boost::unordered_flat_map<RelSig, RelId>& reverseRelMap();
    boost::unordered_flat_map<std::string, RelNameId>& relNameMap();
    boost::unordered_flat_map<EntId, const std::vector<std::string>*>& entitySchemaMap();
    boost::unordered_flat_map<EntId, const std::vector<RelId>*>& srcEntRelMap();
    boost::unordered_flat_map<EntId, const std::vector<RelId>*>& destEntRelMap();
    boost::unordered_flat_map<RelId, char>& referenceMap();
    boost::unordered_flat_map<RelSig,std::string>& relRetentionMap();
    std::vector<std::unique_ptr<std::vector<EntId>>>& vectors();
    std::vector<std::unique_ptr<std::vector<RelId>>>& vectors2();
    
    // const access methods for getting underlying hash maps
    const boost::unordered_flat_map<std::string, EntityEntry>& entityMap() const;
    const boost::unordered_flat_map<EntId, bool>& fileLists() const;
    const boost::unordered_flat_map<EntId, const std::vector<EntId>*>& ancestorMap() const;
    const boost::unordered_flat_map<EntId, const std::vector<EntId>*>& descendantMap() const;
    const boost::unordered_flat_map<RelSig, RelId>& reverseRelMap() const;
    const boost::unordered_flat_map<std::string, RelNameId>& relNameMap() const;
    const boost::unordered_flat_map<EntId, const std::vector<std::string>*>& entitySchemaMap() const;
    const boost::unordered_flat_map<EntId, const std::vector<RelId>*>& srcEntRelMap() const;
    const boost::unordered_flat_map<EntId, const std::vector<RelId>*>& destEntRelMap() const;
    const boost::unordered_flat_map<RelId, char>& referenceMap() const;
    const boost::unordered_flat_map<RelSig,std::string>& relRetentionMap() const;

private:
    static std::unique_ptr<CatCache> instance_;

    CatCache();
    bool refreshImpl();
    bool refreshEntityMap(PGconn* pg_conn);
    bool refreshEntitySchemaMap(PGconn* pg_conn);
    bool refreshAncDesc(PGconn* pg_conn);
    bool refreshReverseRelMap(PGconn* pg_conn);
    bool refreshRelNameMap(PGconn* pg_conn);
    bool refreshRelRetentionMap(PGconn* pg_conn);
    void refreshSrcDestRel();
    

    // Maps entity name to its entry
    boost::unordered_flat_map<std::string, EntityEntry> entity_map_;
    // Whether the entity is also file list
    boost::unordered_flat_map<EntId, bool> file_lists_;
    // Maps entity id to list of its ancestors
    boost::unordered_flat_map<EntId, const std::vector<EntId>*> ancestor_map_;
    // Maps entity id to list of its descendants 
    boost::unordered_flat_map<EntId, const std::vector<EntId>*> descendant_map_;
    // Reverse index that maps all possible rel signatures to rel id. 
    boost::unordered_flat_map<RelSig, RelId> reverse_rel_map_;
    // Maps relation name to name id
    boost::unordered_flat_map<std::string, RelNameId> rel_name_map_;
    // maps entity id to list of column names
    boost::unordered_flat_map<EntId, const std::vector<std::string>*> entity_schema_map_;
    // Maps source entity id to list of relation ids
    boost::unordered_flat_map<EntId, const std::vector<RelId>*> src_ent_rel_map_;
    // Maps dest entity id to list of relation ids
    boost::unordered_flat_map<EntId, const std::vector<RelId>*> dest_ent_rel_map_;
    // Maps rel id to whether the rel type is referential 
    boost::unordered_flat_map<RelId, char> reference_map_;
    // Mapping from relation signature to its retention period.
    boost::unordered_flat_map<RelSig,std::string> rel_retention_map_;
    // List of vectors, so they are deallocated when CatCache is destructed.
    std::vector<std::unique_ptr<std::vector<EntId>>> vectors_;
    // List of vectors, so they are deallocated when CatCache is destructed.
    std::vector<std::unique_ptr<std::vector<RelId>>> vectors2_;
    // List of vectors, so they are deallocated when CatCache is destructed.
    std::vector<std::unique_ptr<std::vector<std::string>>> vectors3_;
};

}