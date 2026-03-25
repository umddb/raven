#include <iostream>
#include <vector>

#include <endian.h>
#include "utils/catcache.h"

namespace ercat {

bool CatCache::init() {
    return refresh();
}

bool CatCache::refresh() {
    std::unique_lock<std::shared_mutex>(mtx());
    CatCache* cat_cache = new CatCache();
    // updating metadata from Postgres failed.
    if (!cat_cache->refreshImpl()) {
        delete cat_cache;
        return false;
    }
    instance_.reset(cat_cache);
    return true;
}

const CatCache& CatCache::getInstance() {
    return *instance_;
}

std::shared_mutex& CatCache::mtx() {
    static PaddedSharedMutex mtx;
    return mtx.mtx_;
}

CatCache::~CatCache() { }

boost::unordered_flat_map<std::string, EntityEntry>& CatCache::entityMap() {
    return entity_map_;
}

boost::unordered_flat_map<EntId, bool>& CatCache::fileLists() {
    return file_lists_;
}

boost::unordered_flat_map<EntId, const std::vector<EntId>*>& CatCache::ancestorMap() {
    return ancestor_map_;
}

boost::unordered_flat_map<EntId, const std::vector<EntId>*>& CatCache::descendantMap() {
    return descendant_map_;
}

boost::unordered_flat_map<RelSig, RelId>& CatCache::reverseRelMap() {
    return reverse_rel_map_;
}

boost::unordered_flat_map<std::string, RelNameId>& CatCache::relNameMap() {
    return rel_name_map_;
}

boost::unordered_flat_map<EntId, const std::vector<std::string>*>& CatCache::entitySchemaMap() {
    return entity_schema_map_;
}

boost::unordered_flat_map<EntId, const std::vector<RelId>*>& CatCache::srcEntRelMap() {
    return src_ent_rel_map_;   
}

boost::unordered_flat_map<EntId, const std::vector<RelId>*>& CatCache::destEntRelMap() {
    return dest_ent_rel_map_;
}

boost::unordered_flat_map<RelId, char>& CatCache::referenceMap() {
    return reference_map_;
}

boost::unordered_flat_map<RelSig,std::string>& CatCache::relRetentionMap() {
    return rel_retention_map_;
}

std::vector<std::unique_ptr<std::vector<EntId>>>& CatCache::vectors() {
    return vectors_;
}

std::vector<std::unique_ptr<std::vector<EntId>>>& CatCache::vectors2() {
    return vectors2_;
}

const boost::unordered_flat_map<std::string, EntityEntry>& CatCache::entityMap() const {
    return entity_map_;
}

const boost::unordered_flat_map<EntId, bool>& CatCache::fileLists() const {
    return file_lists_;
}

const boost::unordered_flat_map<EntId, const std::vector<EntId>*>& CatCache::ancestorMap() const {
    return ancestor_map_;
}

const boost::unordered_flat_map<EntId, const std::vector<EntId>*>& CatCache::descendantMap() const {
    return descendant_map_;
}

const boost::unordered_flat_map<RelSig, RelId>& CatCache::reverseRelMap() const {
    return reverse_rel_map_;
}

const boost::unordered_flat_map<std::string, RelNameId>& CatCache::relNameMap() const {
    return rel_name_map_;
}

const boost::unordered_flat_map<EntId, const std::vector<std::string>*>& CatCache::entitySchemaMap() const {
    return entity_schema_map_;
}

const boost::unordered_flat_map<EntId, const std::vector<RelId>*>& CatCache::srcEntRelMap() const {
    return src_ent_rel_map_;   
}

const boost::unordered_flat_map<EntId, const std::vector<RelId>*>& CatCache::destEntRelMap() const {
    return dest_ent_rel_map_;
}

const boost::unordered_flat_map<RelId, char>& CatCache::referenceMap() const {
    return reference_map_;
}

const boost::unordered_flat_map<RelSig,std::string>& CatCache::relRetentionMap() const {
    return rel_retention_map_;
}

std::unique_ptr<CatCache> CatCache::instance_;

CatCache::CatCache() { }

bool CatCache::refreshImpl() {
    std::string pg_conn_str = Config::getPGConnString();
    PGconn* pg_conn = PQconnectdb(pg_conn_str.c_str());
    
    if (PQstatus(pg_conn) != CONNECTION_OK) {
        std::cerr << "Warning: CatCache refresh failed due to connection failure.\n";
        PQfinish(pg_conn);
        return false;
    }

    // PGresult* res = PQexec(pg_conn, "SELECT pg_catalog.set_config('search_path', '', false)");
    // if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    //     std::cerr << "Warning: CatCache refresh failed due to Postgres Error." 
    //         << PQerrorMessage(pg_conn) << "\n";
    //     PQclear(res);
    //     PQfinish(pg_conn);
    //     return;
    // }

    if (!refreshEntityMap(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    if (!refreshEntitySchemaMap(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    if (!refreshAncDesc(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    if (!refreshReverseRelMap(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    if (!refreshRelNameMap(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    if (!refreshRelRetentionMap(pg_conn)) {
        PQfinish(pg_conn);
        return false;
    }

    refreshSrcDestRel();

    PQfinish(pg_conn);
    return true;

}

bool CatCache::refreshEntityMap(PGconn* pg_conn) {
    PGresult* res = PQexecParams(pg_conn, "SELECT ent_id, ent_name, is_abstract, is_file_list, is_root FROM ercat.Entity", 0,
            NULL, NULL, NULL, NULL, 1);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Error: Failed to refresh EntityMap\n";
        PQclear(res);
        return false;
    }
    
    entity_map_.clear();
    int ent_id_fnum = PQfnumber(res, "ent_id");
    int ent_name_fnum = PQfnumber(res, "ent_name");
    int is_abstract_fnum = PQfnumber(res, "is_abstract");
    int is_file_list_fnum = PQfnumber(res, "is_file_list");
    int is_root_fnum = PQfnumber(res, "is_root");
    for (int i = 0; i < PQntuples(res); i++) {
        EntityEntry ent_entry;
        ent_entry.ent_id_ = be32toh(*((uint32_t *) PQgetvalue(res, i, ent_id_fnum)));
        ent_entry.is_abstract_ = *PQgetvalue(res, i, is_abstract_fnum);
        ent_entry.is_file_list_ = *PQgetvalue(res, i, is_file_list_fnum);
        ent_entry.is_root_ = *PQgetvalue(res, i, is_root_fnum);
        char* ent_name = PQgetvalue(res, i, ent_name_fnum);
        entity_map_.emplace(std::string(ent_name), ent_entry);
        file_lists_.emplace(ent_entry.ent_id_, *PQgetvalue(res, i, is_file_list_fnum));
    }

    PQclear(res);

    // //TODO figuring out how to copy result so it can be streamed to the client
    // res = PQexec(pg_conn, "COPY (SELECT ent_id, is_abstract, ent_name FROM ercat.Entity) TO STDOUT (FORMAT BINARY)");
    // if (PQresultStatus(res) != PGRES_COPY_OUT) {
    //     std::cerr << "copy failed!!\n";
    //     PQclear(res);
    //     return false;
    // }

    // PQclear(res);

    // int ret;
    // char *buf;
    // long total_bytes_written = 0;
    // while ((ret = PQgetCopyData(pg_conn, &buf, 0)) > 0) {
    //     total_bytes_written += ret;
    //     std::cout << ret << " bytes\n";
    //     PQfreemem(buf); // Free the buffer allocated by PQgetCopyData
    // }

    return true;
}

bool CatCache::refreshEntitySchemaMap(PGconn* pg_conn) {
    vectors3_.clear();

    for (auto elem : entity_map_) {
        EntId ent_id = elem.second.ent_id_;
        std::string schema_query;
        fmt::format_to(
            std::back_inserter(schema_query),
            "SELECT column_name "
            "FROM information_schema.columns "
            "WHERE table_schema = 'public' AND table_name = 'ent{}' "
            "ORDER BY ordinal_position",
            ent_id
        );

        PGresult* res = PQexecParams(pg_conn, schema_query.c_str(), 0,
                NULL, NULL, NULL, NULL, 1);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::cerr << "Error: Failed to refresh EntitySchema!\n";
            PQclear(res);
            return false;
        }

        vectors3_.push_back(std::make_unique<std::vector<std::string>>());
        entity_schema_map_.emplace(ent_id, vectors3_.back().get());
        std::vector<std::string>& schema = *vectors3_.back().get();

        int column_name_fnum = PQfnumber(res, "column_name");
        for (int i = 0; i < PQntuples(res); i++) {
            char* column_name = PQgetvalue(res, i, column_name_fnum);
            schema.emplace_back(column_name);
        }

        PQclear(res);
    }
    
    return true;
}

bool CatCache::refreshAncDesc(PGconn* pg_conn) {
    PGresult* res = PQexecParams(pg_conn, "SELECT src_ent_id, dest_ent_id FROM ercat.Implements", 0, NULL, NULL, NULL,
            NULL, 1);
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Error: Failed to refresh AncestorMap and DescendantMap\n";
        PQclear(res);
        return false;
    }

    ancestor_map_.clear();
    descendant_map_.clear();
    vectors_.clear();
    // assume that entity map has already been refreshed. Every concrete entity has itself as ancestor & descendant
    for (const auto& elem : entity_map_) {
        bool is_abstract = elem.second.is_abstract_;
        EntId ent_id = elem.second.ent_id_;
        vectors_.push_back(std::make_unique<std::vector<EntId>>());
        if (!is_abstract) {
            vectors_.back()->push_back(ent_id);
        }
        ancestor_map_.emplace(ent_id, vectors_.back().get());
        vectors_.push_back(std::make_unique<std::vector<EntId>>());
        if (!is_abstract) {
            vectors_.back()->push_back(ent_id);
        }
        descendant_map_.emplace(ent_id, vectors_.back().get());
    }

    int src_ent_id_fnum = PQfnumber(res, "src_ent_id");
    int dest_ent_id_fnum = PQfnumber(res, "dest_ent_id");
    for (int i = 0; i < PQntuples(res); i++) {
        EntId src_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, src_ent_id_fnum)));
        EntId dest_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, dest_ent_id_fnum)));
        if (!(ancestor_map_.contains(src_ent_id) && descendant_map_.contains(dest_ent_id))) {
            std::cerr << "Error: inconsistency in Implements table\n";
            PQclear(res);
            return false;
        }
        const_cast<std::vector<EntId>*>(ancestor_map_.at(src_ent_id))->push_back(dest_ent_id);
        const_cast<std::vector<EntId>*>(descendant_map_.at(dest_ent_id))->push_back(src_ent_id);
    }
    
    PQclear(res);
    return true;
}

bool CatCache::refreshReverseRelMap(PGconn* pg_conn) {
    PGresult* res = PQexecParams(pg_conn, "SELECT rel_id, rel_name_id, src_ent_id, dest_ent_id, is_referential FROM ercat.Relationship",
            0, NULL, NULL, NULL, NULL, 1);
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Error: Failed to refresh ReverseRelMap\n";
        PQclear(res);
        return false;
    }

    reverse_rel_map_.clear();
    int rel_id_fnum = PQfnumber(res, "rel_id");
    int rel_name_id_fnum = PQfnumber(res, "rel_name_id");
    int src_ent_id_fnum = PQfnumber(res, "src_ent_id");
    int dest_ent_id_fnum = PQfnumber(res, "dest_ent_id");
    int is_referential_fnum = PQfnumber(res, "is_referential");
    for (int i = 0; i < PQntuples(res); i++) {
        RelId rel_id = be32toh(*((uint32_t *) PQgetvalue(res, i, rel_id_fnum)));
        RelNameId rel_name_id = be32toh(*((uint32_t *) PQgetvalue(res, i, rel_name_id_fnum)));
        EntId base_src_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, src_ent_id_fnum)));
        EntId base_dest_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, dest_ent_id_fnum)));
        char is_referential = PQgetvalue(res, i, is_referential_fnum)[0];
        reverse_rel_map_.emplace(RelSig(rel_name_id, base_src_ent_id, base_dest_ent_id), rel_id);
        for (const auto& src_ent_id : *descendant_map_.at(base_src_ent_id)) {
            reverse_rel_map_.emplace(RelSig(rel_name_id, src_ent_id, base_dest_ent_id), rel_id);
            for (const auto& dest_ent_id : *descendant_map_.at(base_dest_ent_id)) {
                reverse_rel_map_.emplace(RelSig(rel_name_id, base_src_ent_id, dest_ent_id), rel_id);
                reverse_rel_map_.emplace(RelSig(rel_name_id, src_ent_id, dest_ent_id), rel_id);
            }    
        }
        reference_map_.emplace(rel_id, is_referential);
    }

    PQclear(res);
    return true;
}

bool CatCache::refreshRelNameMap(PGconn* pg_conn) {
    PGresult* res = PQexecParams(pg_conn, "SELECT rel_name, rel_name_id FROM ercat.RelationshipName", 0, NULL, NULL, NULL,
            NULL, 1);
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Error: Failed to refresh RelNameMap\n";
        PQclear(res);
        return false;
    }

    rel_name_map_.clear();
    int rel_name_fnum = PQfnumber(res, "rel_name");
    int rel_name_id_fnum = PQfnumber(res, "rel_name_id");
    for (int i = 0; i < PQntuples(res); i++) {
        char* rel_name = PQgetvalue(res, i, rel_name_fnum);
        RelNameId rel_name_id = be32toh(*((uint32_t *) PQgetvalue(res, i, rel_name_id_fnum)));
        rel_name_map_.emplace(std::string(rel_name), rel_name_id);
    }

    PQclear(res);
    return true;
}

bool CatCache::refreshRelRetentionMap(PGconn* pg_conn) {
    PGresult* res = PQexecParams(pg_conn, 
            "SELECT r.rel_name_id, c.src_ent_id, c.dest_ent_id, c.retention_period FROM ercat.Relationship r "
                "INNER JOIN ercat.RelationshipConstraints c ON c.rel_id = r.rel_id",
            0, NULL, NULL, NULL, NULL, 1);
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Error: Failed to refresh RelRetentionMap\n";
        PQclear(res);
        return false;
    }

    rel_retention_map_.clear();
    int rel_name_id_fnum = PQfnumber(res, "rel_name_id");
    int src_ent_id_fnum = PQfnumber(res, "src_ent_id");
    int dest_ent_id_fnum = PQfnumber(res, "dest_ent_id");
    int retention_period_fnum = PQfnumber(res, "retention_period");
    for (int i = 0; i < PQntuples(res); i++) {
        RelNameId rel_name_id = be32toh(*((uint32_t *) PQgetvalue(res, i, rel_name_id_fnum)));
        EntId base_src_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, src_ent_id_fnum)));
        EntId base_dest_ent_id = be32toh(*((uint32_t *) PQgetvalue(res, i, dest_ent_id_fnum)));
        std::string retention_period = "'" + std::string(PQgetvalue(res, i, retention_period_fnum)) + "'";
        rel_retention_map_.emplace(RelSig(rel_name_id, base_src_ent_id, base_dest_ent_id), retention_period);
    }

    PQclear(res);
    return true;
}

void CatCache::refreshSrcDestRel() {
    src_ent_rel_map_.clear();
    dest_ent_rel_map_.clear();
    vectors2_.clear();
    for (const auto& elem : reverse_rel_map_) {
        EntId src_ent_id = elem.first.src_ent_id_;
        EntId dest_ent_id = elem.first.dest_ent_id_;
        RelId rel_id = elem.second;
        if (!src_ent_rel_map_.contains(src_ent_id)) {
            vectors2_.push_back(std::make_unique<std::vector<RelId>>());
            src_ent_rel_map_.emplace(src_ent_id, vectors2_.back().get());
        }
        if (!dest_ent_rel_map_.contains(dest_ent_id)) {
            vectors2_.push_back(std::make_unique<std::vector<RelId>>());
            dest_ent_rel_map_.emplace(dest_ent_id, vectors2_.back().get());
        }
        // a bit of brueforce, but should be ok
        // rel_id can be inserted multiple times because of different combinations of ent_id
        std::vector<RelId>* src_ent_rel_ids = const_cast<std::vector<RelId>*>(src_ent_rel_map_.at(src_ent_id));
        if (std::find(src_ent_rel_ids->begin(), src_ent_rel_ids->end(), rel_id) == src_ent_rel_ids->end()) {
            src_ent_rel_ids->push_back(rel_id);
        }
        std::vector<RelId>* dest_ent_rel_ids = const_cast<std::vector<RelId>*>(dest_ent_rel_map_.at(dest_ent_id));
        if (std::find(dest_ent_rel_ids->begin(), dest_ent_rel_ids->end(), rel_id) == dest_ent_rel_ids->end()) {
            dest_ent_rel_ids->push_back(rel_id);
        }
    }
}


}