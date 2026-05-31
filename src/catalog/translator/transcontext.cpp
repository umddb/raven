#include <algorithm>
#include <limits>

#include "catalog/translator/joingen.h"
#include "catalog/translator/transcontext.h"
#include "utils/stringutil.h"

namespace ercat {

TransContext::TransContext(TransContextType trans_ctx_type) : trans_ctx_type_(trans_ctx_type), start_idx_(0), end_idx_(0) { }

TransContext::~TransContext() { }

TransContextType TransContext::transContextType() const { return trans_ctx_type_; }

size_t TransContext::startIdx() const { return start_idx_; }
    
size_t TransContext::endIdx() const { return end_idx_; }

const std::vector<std::unique_ptr<TransContext>>& TransContext::children() const { return children_; }

void TransContext::setStartIdx(size_t start_idx) { start_idx_ = start_idx; }

void TransContext::setEndIdx(size_t end_idx) { end_idx_ = end_idx; }

void TransContext::addChild(TransContext * child_ctx) {  children_.push_back(std::unique_ptr<TransContext>(child_ctx));  };

void TransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    size_t cur_idx = start_idx_;
    for (auto& child : children_) {
        size_t next_idx = child->startIdx();
        // output string before the next child
        if (cur_idx < next_idx) {
            for (auto& output_stream : output_streams) {
                output_stream.append(std::string_view(input_string).substr(cur_idx, next_idx - cur_idx));
            }
            cur_idx = next_idx;
        }
        child->translate(input_string, output_streams, errors);
        cur_idx = child->endIdx() + 1;
    }
    // handle remaining string within the current context
    if (cur_idx <= end_idx_) {
        for (auto& output_stream : output_streams) {
            output_stream.append(std::string_view(input_string).substr(cur_idx, end_idx_ + 1 - cur_idx));
        }
    }
}

StmtBlockTransContext::StmtBlockTransContext() : TransContext(TransContextType::StmtBlock) { }

StmtBlockTransContext::~StmtBlockTransContext() { }

AlterEntStmtTransContext::AlterEntStmtTransContext() : TransContext(TransContextType::AlterEntStmt) { }

AlterEntStmtTransContext::~AlterEntStmtTransContext() { }

void AlterEntStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const auto& entity_map = CatCache::getInstance().entityMap();
    if (entity_map.contains(ent_name_)) {
        std::string output_str;
        fmt::format_to(
            std::back_inserter(output_str),
            "UPDATE ercat.Entity SET is_root = TRUE WHERE ent_id = {}",
            entity_map.at(ent_name_).ent_id_
        );
        output_streams.clear();
        output_streams.push_back(output_str);
    }
    else {
        errors.emplace_back("Invalid Entity name " + ent_name_);
    }
}

AlterRelStmtTransContext::AlterRelStmtTransContext() : TransContext(TransContextType::AlterRelStmt) { }

AlterRelStmtTransContext::~AlterRelStmtTransContext() { }

void AlterRelStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& rel_name_map = cat_cache.relNameMap();
    const auto& reverse_rel_map = cat_cache.reverseRelMap();

    // basic checks
    if (!rel_name_map.contains(rel_name_)) {
        errors.emplace_back(rel_name_ + " is invalid relation name.");
        return;
    }
    if (!ent_map.contains(src_ent_)) {
        errors.emplace_back(src_ent_ + " is invalid entity type.");
        return;
    }
    if (!ent_map.contains(dest_ent_)) {
        errors.emplace_back(dest_ent_ + " is invalid entity type.");
        return;
    }

    RelSig rel_sig(rel_name_map.at(rel_name_), ent_map.at(src_ent_).ent_id_,
        ent_map.at(dest_ent_).ent_id_);
        
    if(!reverse_rel_map.contains(rel_sig)) {
        errors.emplace_back(rel_sig.toString() + " is invalid relation signature");
        return;
    }
    RelId rel_id = reverse_rel_map.at(rel_sig);
    bool right_ref = (src_ent_ == referrer_ent_ && dest_ent_ == referee_ent_);
    bool left_ref = (src_ent_ == referee_ent_ && dest_ent_ == referrer_ent_);

    if ((right_ref || left_ref) && createsCycle(rel_id, right_ref, left_ref)) {
        errors.emplace_back("Altering " + rel_sig.toString() + " creates a cycle in ER model graph");
        return;
    }

    std::string output_str;
    // reference going to the right
    if (right_ref) {
        fmt::format_to(
            std::back_inserter(output_str),
            "UPDATE ercat.Relationship SET is_referential = 'r' WHERE rel_id = {}",
            rel_id
        );
    }
    // reference going to the left
    else if (left_ref) {
        fmt::format_to(
            std::back_inserter(output_str),
            "UPDATE ercat.Relationship SET is_referential = 'l' WHERE rel_id = {}",
            rel_id
        );
    }
    // set the retention period
    else if (!retention_period_.empty()) {
        fmt::format_to(
            std::back_inserter(output_str),
            "INSERT INTO "
            "ercat.RelationshipConstraints(constraint_name, rel_id, src_ent_id, dest_ent_id, retention_period) "
            "VALUES ('{}', {}, {}, {}, {})",
            constraint_name_,
            rel_id,
            rel_sig.src_ent_id_,
            rel_sig.dest_ent_id_,
            retention_period_
        );
    }
    else {
        errors.emplace_back("Constraint is invalid");
        return;
    }

    output_streams.clear();
    output_streams.push_back(output_str);
}

bool AlterRelStmtTransContext::createsCycle(RelId new_rel_id, bool right_ref, bool left_ref) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& reverse_rel_map = cat_cache.reverseRelMap();
    const auto& reference_map = cat_cache.referenceMap();
    // for convenience we pool all the abstract and concrete types
    boost::unordered_flat_map<EntId, boost::unordered_flat_set<EntId>> reference_graph;
    // enumerate all the relation signatures
    for (const auto& elem : reverse_rel_map) {
        RelSig rel_sig = elem.first;
        RelId rel_id = elem.second;
        // source vertex
        EntId src_vertex;
        // destination vertex
        EntId dest_vertex;
        // if the target relation type, alter the referential information
        if (new_rel_id == rel_id) {
            if (right_ref) {
                src_vertex = rel_sig.src_ent_id_;
                dest_vertex = rel_sig.dest_ent_id_;
            }
            else {
                src_vertex = rel_sig.dest_ent_id_;
                dest_vertex = rel_sig.src_ent_id_;
            }
        }
        else {
            // check the direction of the reference 
            if (reference_map.at(rel_id) == 'r') {
                src_vertex = rel_sig.src_ent_id_;
                dest_vertex = rel_sig.dest_ent_id_;
            }
            else if (reference_map.at(rel_id) == 'l') {
                src_vertex = rel_sig.dest_ent_id_;
                dest_vertex = rel_sig.src_ent_id_;
            }
            // not a reference, so pass
            else {
                continue;
            }
        }
        
        // new entry if the vertex does not exist. 
        reference_graph.try_emplace(src_vertex);
        
        // get the adjacency list and insert
        auto& neighbors = reference_graph.at(src_vertex);
        neighbors.insert(dest_vertex);
    }

    boost::unordered_flat_map<EntId, VisitState> states;
    // Iterate through all nodes in the graph to handle disconnected components
    for (const auto& pair : reference_graph) {
        EntId node = pair.first;
        
        // If the node hasn't been visited, start a DFS from it
        if (states.find(node) == states.end() || states[node] == VisitState::UNVISITED) {
            if (hasCycleDFS(node, reference_graph, states)) {
                return true;
            }
        }
    }
    
    return false;

}

bool AlterRelStmtTransContext::hasCycleDFS(const EntId node, 
                 const boost::unordered_flat_map<EntId, boost::unordered_flat_set<EntId>>& graph, 
                 boost::unordered_flat_map<EntId, VisitState>& states) const {
    
    // Mark the current node as currently being visited (in the recursion stack)
    states[node] = VisitState::VISITING;
    
    // Find neighbors of the current node
    auto it = graph.find(node);
    if (it != graph.end()) {
        for (const EntId neighbor : it->second) {
            
            // Check the state of the neighbor
            auto state_it = states.find(neighbor);
            VisitState neighbor_state = (state_it != states.end()) ? state_it->second : VisitState::UNVISITED;
            
            // If the neighbor is currently being visited, we found a back-edge (cycle)
            // A self-loop will trigger this immediately since the current node is VISITING.
            if (neighbor_state == VisitState::VISITING) {
                return true;
            } 
            // If the neighbor is unvisited, explore it recursively
            else if (neighbor_state == VisitState::UNVISITED) {
                if (hasCycleDFS(neighbor, graph, states)) {
                    return true;
                }
            }
        }
    }
    
    // Mark the node as fully processed
    states[node] = VisitState::VISITED;
    return false;
}

CreateEntStmtTransContext::CreateEntStmtTransContext() : TransContext(TransContextType::CreateEntStmt) { }

CreateEntStmtTransContext::~CreateEntStmtTransContext() { }

void CreateEntStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();

    // get the next ent_id for the new entity
    EntId next_ent_id = 0;
    for (const auto& ent : ent_map) {
        next_ent_id = std::max(next_ent_id, ent.second.ent_id_);
    }
    next_ent_id++;

    // basic checks
    if (ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " already exists.");
        return;
    }

    std::string output_str;
    fmt::format_to(
        std::back_inserter(output_str),
        "INSERT INTO ercat.Entity(ent_id, ent_name, is_abstract, is_file_list, is_root) "
        "VALUES ({}, '{}', {}, {}, {})",
        next_ent_id,
        ent_name_,
        is_abstract_,
        is_file_list_,
        is_root_
    );

    if (!is_abstract_) {
        fmt::format_to(
            std::back_inserter(output_str),
            "; CREATE TABLE ent{} ("
            "obj_id BIGSERIAL,"
            "vid INT4 DEFAULT 0 NOT NULL,",
            next_ent_id
        );

        output_streams.clear();
        output_streams.push_back(output_str);

        for (auto& child : children_) {
            child->translate(input_string, output_streams, errors);
        }
   
        output_streams[0].append(",PRIMARY KEY(obj_id, vid))");
    }
    else {
        output_streams.clear();
        output_streams.push_back(output_str);
    }

    if (!implements_.empty()) {
        if (is_abstract_) {
            errors.emplace_back("An abstract entity cannot implement another abstract entity.");
            return;
        }

        for (auto& elem : implements_) {
            if (!ent_map.contains(elem) || !ent_map.at(elem).is_abstract_) {
                errors.emplace_back(elem + " is not valid abstract entity.");
                return;
            }
            fmt::format_to(
                std::back_inserter(output_streams[0]),
                "; INSERT INTO ercat.Implements(src_ent_id, dest_ent_id) "
                "VALUES ({}, {});",
                next_ent_id,
                ent_map.at(elem).ent_id_
            );
        }
    }    
}

DeleteRootStmtTransContext::DeleteRootStmtTransContext() : TransContext(TransContextType::DeleteRootStmt) { }

DeleteRootStmtTransContext::~DeleteRootStmtTransContext() { }

void DeleteRootStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    
    // basic checks
    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }

    EntityEntry ent_entry = ent_map.at(ent_name_);

    if (!ent_entry.is_root_) {
        errors.emplace_back(ent_name_ + " is not root entity type.");
        return;
    }
    
    bool with_clause = (children_.size() > 0 && children_[0]->trans_ctx_type_ == TransContextType::WithClause); 
    int i = 0;
    if (with_clause) {
        children_[0]->translate(input_string, output_streams, errors);
        i = 1;
    }
    
    for (auto& output_stream : output_streams) {
        if (with_clause) {
            output_stream.append(", DelTemp AS (");
        }
        else {
            output_stream.append("WITH DelTemp AS (");
        }

        fmt::format_to(
            std::back_inserter(output_stream),
            "SELECT obj_id, vid FROM ent{} ",
            ent_entry.ent_id_
        );
    }
    
    for (; i < children_.size(); i++) {
        children_[i]->translate(input_string, output_streams, errors);
    }

    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            ") INSERT INTO ercat.DeleteEdges "
                "(delete_time, epoch_id, src_ent_id, src_vid, src_obj_id, dest_ent_id, dest_vid, dest_obj_id) "
                "SELECT NOW(), e.epoch_id, 0, 0, 0, {}, t.vid, t.obj_id FROM ercat.EpochId e, DelTemp t",
                ent_entry.ent_id_
        );
    }
    
}

DeleteRelStmtTransContext::DeleteRelStmtTransContext() : TransContext(TransContextType::DeleteRelStmt) { }

DeleteRelStmtTransContext::~DeleteRelStmtTransContext() { }

void DeleteRelStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& rel_name_map = cat_cache.relNameMap();
    const auto& reverse_rel_map = cat_cache.reverseRelMap();
    const auto& reference_map = cat_cache.referenceMap();
    const auto& rel_retention_map = cat_cache.relRetentionMap();

    if (!rel_name_map.contains(rel_name_)) {
        errors.emplace_back(rel_name_ + " is invalid relation name.");
        return;
    }
    if (!ent_map.contains(src_ent_)) {
        errors.emplace_back(src_ent_ + " is invalid entity type.");
        return;
    }
    if (!ent_map.contains(dest_ent_)) {
        errors.emplace_back(dest_ent_ + " is invalid entity type.");
        return;
    }

    RelSig rel_sig(rel_name_map.at(rel_name_), ent_map.at(src_ent_).ent_id_, ent_map.at(dest_ent_).ent_id_);
    if (!reverse_rel_map.contains(rel_sig)) {
        errors.emplace_back(rel_sig.toString() + " is invalid relationship type.");
        return;
    }
    RelId rel_id = reverse_rel_map.at(rel_sig);
    bool right_reference = (reference_map.at(rel_id) == 'r');
    bool left_reference = (reference_map.at(rel_id) == 'l');
    bool with_clause = (children_.size() > 0 && children_[0]->trans_ctx_type_ == TransContextType::WithClause);
    
    int i = 0;
    if (with_clause) {
        i = 1;
        children_[0]->translate(input_string, output_streams, errors);
    }

    std::string retention_period = "'0 day'";
    if (!retention_.empty()) {
        retention_period = retention_;
    }
    else if (rel_retention_map.contains(rel_sig)) {
        retention_period = rel_retention_map.at(rel_sig);
    }

    for (auto& output_stream : output_streams) {
        if (right_reference || left_reference) {
            if (with_clause) {
                output_stream.append(", DelTemp AS (");
            }
            else {
                output_stream.append("WITH DelTemp AS (");
            }
        }

        fmt::format_to(
            std::back_inserter(output_stream),
            " UPDATE rel{0} SET delete_time = NOW() + INTERVAL {1} ",
            rel_id, retention_period
        );

        if (with_clause) {
            output_stream.append(" FROM ");
            auto& cte_ctx = children_[0]->children_;
            fmt::format_to(
                std::back_inserter(output_stream),
                " {} ",
                static_cast<CommonTableExprTransContext*>(cte_ctx[0].get())->name_
                );
            for (int j = 1; j < cte_ctx.size(); j++) {
                fmt::format_to(
                std::back_inserter(output_stream),
                ", {} ",
                static_cast<CommonTableExprTransContext*>(cte_ctx[i].get())->name_
                );
            }
        }
    }

    bool where_clause = false;
    for (; i < children_.size(); i++) {
        if (children_[i]->trans_ctx_type_ == TransContextType::WhereClause) {
            where_clause = true;
        }
        children_[i]->translate(input_string, output_streams, errors);
    }

    for (auto& output_stream : output_streams) {
        if (where_clause) {
            output_stream.append(" AND ");
        }
        else {
            output_stream.append(" WHERE ");
        }

        fmt::format_to(
            std::back_inserter(output_stream),
            " src_ent_id = {} AND dest_ent_id = {} AND delete_time IS NULL ",
            rel_sig.src_ent_id_,
            rel_sig.dest_ent_id_
        );
    
        if (right_reference) {
            output_stream.append(" RETURNING *) INSERT INTO ercat.DeleteEdges "
                "(delete_time, epoch_id, src_ent_id, src_vid, src_obj_id, dest_ent_id, dest_vid, dest_obj_id) "
                "SELECT t.delete_time, e.epoch_id, t.src_ent_id, t.src_vid, t.src_obj_id, t.dest_ent_id, t.dest_vid, "
                "t.dest_obj_id FROM ercat.EpochId e, DelTemp t");   
        }
        else if (left_reference) {
            output_stream.append(" RETURNING *) INSERT INTO ercat.DeleteEdges "
                "(delete_time, epoch_id, dest_ent_id, dest_vid, dest_obj_id, src_ent_id, src_vid, src_obj_id) "
                "SELECT t.delete_time, e.epoch_id, t.src_ent_id, t.src_vid, t.src_obj_id, t.dest_ent_id, t.dest_vid, "
                "t.dest_obj_id FROM ercat.EpochId e, DelTemp t");
        }
    }
}

DeleteFileStmtTransContext::DeleteFileStmtTransContext() : TransContext(TransContextType::DeleteFileStmt) { }

DeleteFileStmtTransContext::~DeleteFileStmtTransContext() { }

void DeleteFileStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& ent_schema_map = cat_cache.entitySchemaMap();

    // basic checks
    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }
    EntId ent_id = ent_map.at(ent_name_).ent_id_;
    const auto* ent_schema = ent_schema_map.at(ent_id);
    std::string column_names;
    for (const auto& column : *ent_schema) {
        if (column != "vid") {
            column_names.append(", " + column);
        }
    }

    size_t partition_id = hash_value(FileListId(ent_id, obj_id_)) % 1024;
    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            "WITH DelTemp (path) AS ({0}) "
            "UPDATE ercat.Files_{1} "
            "SET end_vid = {2} + 1 "
            "FROM DelTemp d "
            "WHERE NOT EXISTS (SELECT * FROM ent{3} WHERE obj_id = {4} AND vid > {2}) "
            "AND EXISTS (SELECT * FROM ent{3} WHERE obj_id = {4} AND vid = {2}) "
            "AND ent_id = {3} AND obj_id = {4} "
            "AND file_path = d.path AND start_vid <= {2} AND end_vid > {2}",
            std::string_view(input_string).substr(values_clause_.first, values_clause_.second - values_clause_.first + 1), // {0}
            partition_id,  // {1}
            vid_,          // {2}
            ent_id,        // {3}
            obj_id_,       // {4}
            column_names   // {5}
        );

        if (!prepare_) {
            fmt::format_to(
                std::back_inserter(output_stream),
                "; INSERT INTO ent{0} (vid {1}) "
                "SELECT {2} + 1 {1} FROM ent{0} WHERE obj_id = {3} AND vid = {2}",
                ent_id,         // {0}
                column_names,   // {1}
                vid_,           // {2}
                obj_id_         // {3}
            );
        }
    }

}

WhereClauseTransContext::WhereClauseTransContext() : TransContext(TransContextType::WhereClause) { }

WhereClauseTransContext::~WhereClauseTransContext() { }

TableElementTransContext::TableElementTransContext() : TransContext(TransContextType::TableElement) { }

TableElementTransContext::~TableElementTransContext() { }

void TableElementTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    std::string_view element_list = std::string_view(input_string).substr(start_idx_, end_idx_ + 1 - start_idx_);
    for (auto& output_stream : output_streams) {
        output_stream.append(element_list);
    }
}

CreateRelStmtTransContext::CreateRelStmtTransContext() : TransContext(TransContextType::CreateRelStmt) { }

CreateRelStmtTransContext::~CreateRelStmtTransContext() { }

void CreateRelStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& rel_name_map = cat_cache.relNameMap();
    const auto& reverse_rel_map = cat_cache.reverseRelMap();

    // basic checks
    if (!ent_map.contains(src_ent_)) {
        errors.emplace_back(src_ent_ + " is invalid entity type.");
        return;
    }
    if (!ent_map.contains(dest_ent_)) {
        errors.emplace_back(dest_ent_ + " is invalid entity type.");
        return;
    }

    std::string output_str;
    RelNameId rel_name_id = 0;
    if (rel_name_map.contains(rel_name_)) {
        rel_name_id = rel_name_map.at(rel_name_);
    }
    else {
        for (auto& elem : rel_name_map) {
            rel_name_id = std::max(rel_name_id, elem.second);
        }
        rel_name_id++;
        
        fmt::format_to(
            std::back_inserter(output_str),
            "INSERT INTO ercat.RelationshipName (rel_name_id, rel_name) "
            "VALUES ({}, '{}');",
            rel_name_id,
            rel_name_
        );
    }
    
    EntId src_ent_id = ent_map.at(src_ent_).ent_id_;
    EntId dest_ent_id = ent_map.at(dest_ent_).ent_id_;
    RelSig rel_sig(rel_name_id, src_ent_id, dest_ent_id);
        
    if(reverse_rel_map.contains(rel_sig)) {
        errors.emplace_back(rel_sig.toString() + " already exists.");
        return;
    }

    RelId rel_id = 0;
    for (const auto& rel : reverse_rel_map) {
        rel_id = std::max(rel_id, rel.second);
    }
    rel_id++;

    fmt::format_to(
        std::back_inserter(output_str),
        "INSERT INTO ercat.Relationship (rel_id, rel_name_id, src_ent_id, dest_ent_id) "
        "VALUES ({}, {}, {}, {});",
        rel_id,
        rel_name_id,
        src_ent_id,
        dest_ent_id
    );

    fmt::format_to(
        std::back_inserter(output_str),
        "CREATE TABLE rel{} ("
            "src_ent_id INT4 NOT NULL,"
            "src_vid INT4 NOT NULL,"
            "src_obj_id INT8 NOT NULL,"
            "dest_ent_id INT4 NOT NULL,"
            "dest_vid INT4 NOT NULL,"
            "dest_obj_id INT8 NOT NULL,"
            "delete_time TIMESTAMPTZ,"
            "PRIMARY KEY(src_ent_id, src_vid, src_obj_id, dest_ent_id, dest_vid, dest_obj_id)"
        ");",
        rel_id
    );

    output_streams.clear();
    output_streams.push_back(output_str);
}

SelectPramaryTransContext::SelectPramaryTransContext() : TransContext(TransContextType::SelectPramary) { }

SelectPramaryTransContext::~SelectPramaryTransContext() { }

SelectFileDeltaStmtTransContext::SelectFileDeltaStmtTransContext() : 
        TransContext(TransContextType::SelectFileDelta) { }

SelectFileDeltaStmtTransContext::~SelectFileDeltaStmtTransContext() { }

void SelectFileDeltaStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();

    // basic checks
    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }
    EntId ent_id = ent_map.at(ent_name_).ent_id_;
    TransContext* target_list = children_[0].get();
    size_t partition_id = hash_value(FileListId(ent_id, obj_id_)) % 1024;
    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            "SELECT 'add' AS action, {} "
            "FROM ercat.Files_{} ",
            std::string_view(input_string).substr(target_list->start_idx_, target_list->end_idx_ - 
                    target_list->start_idx_ + 1),
            partition_id
        );
    }

    if (children_.size() > 1) {
        children_[1]->translate(input_string, output_streams, errors);
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " AND ent_id = {} AND obj_id = {} AND start_vid > {} AND start_vid <= {} ",
                ent_id, obj_id_, start_vid_, end_vid_
            );
        }
    }
    else {
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " WHERE ent_id = {} AND obj_id = {} AND start_vid > {} AND start_vid <= {} ",
                ent_id, obj_id_, start_vid_, end_vid_
            );
        }
    }

    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            "UNION ALL SELECT 'delete' AS action, {} "
            "FROM ercat.Files_{} ",
            std::string_view(input_string).substr(target_list->start_idx_, target_list->end_idx_ - 
                    target_list->start_idx_ + 1),
            partition_id
        );
    }

    if (children_.size() > 1) {
        children_[1]->translate(input_string, output_streams, errors);
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " AND ent_id = {} AND obj_id = {} AND end_vid > {} AND end_vid <= {}",
                ent_id, obj_id_, start_vid_, end_vid_
            );
        }
    }
    else {
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " WHERE ent_id = {} AND obj_id = {} AND end_vid > {} AND end_vid <= {}",
                ent_id, obj_id_, start_vid_, end_vid_
            );
        }
    }
}


SelectFileSnapshotStmtTransContext::SelectFileSnapshotStmtTransContext() : 
        TransContext(TransContextType::SelectFileSnapshot) { }

SelectFileSnapshotStmtTransContext::~SelectFileSnapshotStmtTransContext() { }

void SelectFileSnapshotStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();

    // basic checks
    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }
    EntId ent_id = ent_map.at(ent_name_).ent_id_;
    TransContext* target_list = children_[0].get();
    size_t partition_id = hash_value(FileListId(ent_id, obj_id_)) % 1024;
    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            "SELECT {} "
            "FROM ercat.Files_{} ",
            std::string_view(input_string).substr(target_list->start_idx_, target_list->end_idx_ - 
                    target_list->start_idx_ + 1),
            partition_id
        );
    }

    if (children_.size() > 1) {
        children_[1]->translate(input_string, output_streams, errors);
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " AND ent_id = {0} AND obj_id = {1} AND start_vid <= {2} AND end_vid > {2}",
                ent_id, obj_id_, vid_
            );
        }
    }
    else {
        for (auto& output_stream : output_streams) {
            fmt::format_to(
                std::back_inserter(output_stream),
                " WHERE ent_id = {0} AND obj_id = {1} AND start_vid <= {2} AND end_vid > {2}",
                ent_id, obj_id_, vid_
            );
        }
    }
}

WithClauseTransContext::WithClauseTransContext() : TransContext(TransContextType::WithClause) { }

WithClauseTransContext::~WithClauseTransContext() { }

CommonTableExprTransContext::CommonTableExprTransContext() : TransContext(TransContextType::CommonTableExpr) { }

CommonTableExprTransContext::~CommonTableExprTransContext() { }

void SelectPramaryTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    if (children_.empty()) {
        output_streams[0].append(std::string_view(input_string).substr(start_idx_, end_idx_ + 1 - start_idx_));
    }
    else {
        // Use new buffered stream to minimize string copying
        std::vector<std::string> buffered_streams;
        buffered_streams.emplace_back();
        buffered_streams[0].reserve(end_idx_ + 1 - start_idx_);
        size_t cur_idx = start_idx_;
        for (auto& child : children_) {
            size_t next_idx = child->startIdx();
            // output string before the next child
            if (cur_idx < next_idx) {
                for (auto& buffered_stream : buffered_streams) {
                    buffered_stream.append(std::string_view(input_string).substr(cur_idx, next_idx - cur_idx));
                }
                cur_idx = next_idx;
            }
            child->translate(input_string, buffered_streams, errors);
            cur_idx = child->endIdx() + 1;
        }
        // handle remaining string within the current context
        if (cur_idx <= end_idx_) {
            for (auto& buffered_stream : buffered_streams) {
                buffered_stream.append(std::string_view(input_string).substr(cur_idx, end_idx_ + 1 - cur_idx));
            }
        }
        
        // finally merge buffered streams to the main output streams 
        for (auto& output_stream : output_streams) {
            output_stream.append("(");
            for (size_t i = 0; i < buffered_streams.size(); i++) {
                output_stream.append(buffered_streams[i]);
                if (i < buffered_streams.size() - 1) {
                    output_stream.append(" UNION All ");
                }
            }
            output_stream.append(")");
        }
    }
}

TargetListTransContext::TargetListTransContext() : TransContext(TransContextType::TargetList) { }

TargetListTransContext::~TargetListTransContext() { }

void TargetListTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    for (auto& output_stream : output_streams) {
        output_stream.append(target_list_trans_);
    }
}

TargetList1TransContext::TargetList1TransContext() : TransContext(TransContextType::TargetList1) { }

TargetList1TransContext::~TargetList1TransContext() { }

FromListTransContext::FromListTransContext() : TransContext(TransContextType::FromList) { }

FromListTransContext::~FromListTransContext() { }

void FromListTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    // save output streams to temporary streams first
    std::vector<std::string> temp_streams;
    temp_streams.reserve(output_streams.size());
    for (auto& output_stream : output_streams) {
        temp_streams.emplace_back(output_stream);
    }
    output_streams.clear();

    JoinGen join_gen(entity_binding_, rel_app_, errors);
    while (join_gen.valid()) {
        std::string from_list = join_gen.next();
        for (auto& temp_stream : temp_streams) {
            output_streams.emplace_back();
            output_streams.back().reserve(temp_stream.size() + from_list.size());
            output_streams.back().append(temp_stream);
            output_streams.back().append(from_list);
        }
    }
}

TransactionStmtTransContext::TransactionStmtTransContext() : TransContext(TransContextType::TransactionStmt) { }

TransactionStmtTransContext::~TransactionStmtTransContext() { }

InsertRootStmtTransContext::InsertRootStmtTransContext() : TransContext(TransContextType::InsertRootStmt) { }

InsertRootStmtTransContext::~InsertRootStmtTransContext() { }

void InsertRootStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();

    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }

    EntityEntry ent_entry = ent_map.at(ent_name_);

    if (!ent_entry.is_root_) {
        errors.emplace_back(ent_name_ + " is not root entity type.");
        return;
    }

    std::string& output_str = output_streams[0];
    fmt::format_to(
        std::back_inserter(output_str),
        "WITH temp AS (INSERT INTO ent{} ",
        ent_entry.ent_id_
    );

    for (auto& child : children_) {
        child->translate(input_string, output_streams, errors);
    }

    fmt::format_to(
        std::back_inserter(output_str),
        " RETURNING obj_id, vid) INSERT INTO ercat.AddEdges "
            "(epoch_id, src_ent_id, src_vid, src_obj_id, dest_ent_id, dest_vid, dest_obj_id) "
            "SELECT e.epoch_id, 0, 0, 0, {}, t.vid, t.obj_id FROM ercat.EpochId e, temp t",
            ent_entry.ent_id_
    );

}

InsertEntStmtTransContext::InsertEntStmtTransContext() : TransContext(TransContextType::InsertEntStmt) { }

InsertEntStmtTransContext::~InsertEntStmtTransContext() { }

void InsertEntStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();

    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }

    EntityEntry ent_entry = ent_map.at(ent_name_);

    int i = 0;
    size_t cur_idx = children_[0]->startIdx();
    if (children_.size() > 0 && children_[0]->trans_ctx_type_ == TransContextType::WithClause) {
        i = 1;
        children_[0]->translate(input_string, output_streams, errors);
        cur_idx = children_[1]->startIdx();
    }

    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            " INSERT INTO ent{} ",
            ent_entry.ent_id_
        );
    }

    for (; i < children_.size(); i++) {
        size_t next_idx = children_[i]->startIdx();
        // output string before the next child
        if (cur_idx < next_idx) {
            for (auto& output_stream : output_streams) {
                output_stream.append(std::string_view(input_string).substr(cur_idx, next_idx - cur_idx));
            }
            cur_idx = next_idx;
        }

        children_[i]->translate(input_string, output_streams, errors);
        cur_idx = children_[i]->endIdx() + 1;

    }

    if (cur_idx <= end_idx_) {
        for (auto& output_stream : output_streams) {
            output_stream.append(std::string_view(input_string).substr(cur_idx, end_idx_ + 1 - cur_idx));
        }
    }

}

InsertRelStmtTransContext::InsertRelStmtTransContext() : TransContext(TransContextType::InsertRelStmt) { }

InsertRelStmtTransContext::~InsertRelStmtTransContext() { }

void InsertRelStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& rel_name_map = cat_cache.relNameMap();
    const auto& reverse_rel_map = cat_cache.reverseRelMap();
    const auto& reference_map = cat_cache.referenceMap();

    // basic checks
    if (!rel_name_map.contains(rel_name_)) {
        errors.emplace_back(rel_name_ + " is invalid relation name.");
        return;
    }
    if (!ent_map.contains(src_ent_)) {
        errors.emplace_back(src_ent_ + " is invalid entity type.");
        return;
    }
    if (!ent_map.contains(dest_ent_)) {
        errors.emplace_back(dest_ent_ + " is invalid entity type.");
        return;
    }

    RelSig rel_sig(rel_name_map.at(rel_name_), ent_map.at(src_ent_).ent_id_, ent_map.at(dest_ent_).ent_id_);
    if (!reverse_rel_map.contains(rel_sig)) {
        errors.emplace_back(rel_sig.toString() + " is invalid relationship type.");
        return;
    }
    RelId rel_id = reverse_rel_map.at(rel_sig);
    bool right_reference = (reference_map.at(rel_id) == 'r');
    bool left_reference = (reference_map.at(rel_id) == 'l');
    
    int i = 0;
    size_t cur_idx = children_[0]->startIdx();
    bool with_clause = (children_.size() > 0 && children_[0]->trans_ctx_type_ == TransContextType::WithClause);
    if (with_clause) {
        i = 1;
        children_[0]->translate(input_string, output_streams, errors);
        cur_idx = children_[1]->startIdx();
    }

    size_t sub_start_idx = std::numeric_limits<size_t>::max();
    for (auto& output_stream : output_streams) {
        if (right_reference || left_reference) {
            if (with_clause) {
                output_stream.append(", RefTemp AS (");
            }
            else {
                output_stream.append("WITH RefTemp AS (");
            }
        }

        fmt::format_to(
            std::back_inserter(output_stream),
            " INSERT INTO rel{} (src_ent_id, src_obj_id, src_vid, dest_ent_id, dest_obj_id, dest_vid) ",
            rel_id
        );
        sub_start_idx = std::min(sub_start_idx, output_stream.size());
    }

    for (; i < children_.size(); i++) {
        size_t next_idx = children_[i]->startIdx();
        // output string before the next child
        if (cur_idx < next_idx) {
            for (auto& output_stream : output_streams) {
                output_stream.append(std::string_view(input_string).substr(cur_idx, next_idx - cur_idx));
            }
            cur_idx = next_idx;
        }

        children_[i]->translate(input_string, output_streams, errors);
        cur_idx = children_[i]->endIdx() + 1;
    }

    if (cur_idx <= end_idx_) {
        for (auto& output_stream : output_streams) {
            output_stream.append(std::string_view(input_string).substr(cur_idx, end_idx_ + 1 - cur_idx));
        }
    }

    // substitute the entity types for the entity id
    for (auto& output_stream : output_streams) {
        if (right_reference) {
            output_stream.append(
                " RETURNING *) INSERT INTO ercat.AddEdges (epoch_id, src_ent_id, src_vid, src_obj_id, "
                "dest_ent_id, dest_vid, dest_obj_id) SELECT e.epoch_id, r.src_ent_id, r.src_vid, "
                "r.src_obj_id, r.dest_ent_id, r.dest_vid, r.dest_obj_id FROM ercat.EpochId e, RefTemp r"
            );
        }
        else if (left_reference) {
            output_stream.append(
                " RETURNING *) INSERT INTO ercat.AddEdges (epoch_id, dest_ent_id, dest_vid, dest_obj_id, "
                "src_ent_id, src_vid, src_obj_id) SELECT e.epoch_id, r.src_ent_id, r.src_vid, "
                "r.src_obj_id, r.dest_ent_id, r.dest_vid, r.dest_obj_id FROM ercat.EpochId e, RefTemp r"
            );
        }

        output_stream = StringUtil::replaceTwoPatterns(
            output_stream,
            std::string("'" + src_ent_ +"'"),
            std::to_string(rel_sig.src_ent_id_),
            std::string("'" + dest_ent_ +"'"),
            std::to_string(rel_sig.dest_ent_id_),
            sub_start_idx
        );
    }
}

InsertFileStmtTransContext::InsertFileStmtTransContext() : TransContext(TransContextType::InsertFileStmt) { }

InsertFileStmtTransContext::~InsertFileStmtTransContext() { }

void InsertFileStmtTransContext::translate(const std::string& input_string,
        std::vector<std::string>& output_streams, std::vector<std::string>& errors) const {
    const CatCache& cat_cache = CatCache::getInstance();
    const auto& ent_map = cat_cache.entityMap();
    const auto& ent_schema_map = cat_cache.entitySchemaMap();

    // basic checks
    if (!ent_map.contains(ent_name_)) {
        errors.emplace_back(ent_name_ + " is invalid entity type.");
        return;
    }
    EntId ent_id = ent_map.at(ent_name_).ent_id_;
    const auto* ent_schema = ent_schema_map.at(ent_id);
    std::string column_names;
    for (const auto& column : *ent_schema) {
        if (column != "vid") {
            column_names.append(", " + column);
        }
    }

    size_t partition_id = hash_value(FileListId(ent_id, obj_id_)) % 1024;
    std::string_view values_view = std::string_view(input_string).substr(values_clause_.first, 
            values_clause_.second - values_clause_.first + 1);
    for (auto& output_stream : output_streams) {
        fmt::format_to(
            std::back_inserter(output_stream),
            "WITH NewTemp ({0}) AS ({1}), "
            "NewTemp2 AS (INSERT INTO ercat.Files_{2} ({0}, partition_id, ent_id, obj_id, start_vid, end_vid) "
            "SELECT {0}, {2}, {3}, {4}, {5} + 1, 2147483647 FROM NewTemp WHERE NOT EXISTS "
            "(SELECT * FROM ent{3} WHERE obj_id = {4} AND vid > {5}) "
            "AND EXISTS (SELECT * FROM ent{3} WHERE obj_id = {4} AND vid = {5}) RETURNING file_path) "
            "INSERT INTO ercat.AddFiles (file_path) SELECT file_path FROM NewTemp2",
            insert_column_list_,        // {0}
            values_view,                // {1}
            partition_id,               // {2}
            ent_id,                     // {3}
            obj_id_,                    // {4}
            vid_,                       // {5}
            column_names                // {6}
        );

        if (!prepare_) {
            fmt::format_to(
                std::back_inserter(output_stream),
                "; INSERT INTO ent{0} (vid {1}) "
                "SELECT {2} + 1 {1} FROM ent{0} WHERE obj_id = {3} AND vid = {2}",
                ent_id,         // {0}
                column_names,   // {1}
                vid_,           // {2}
                obj_id_         // {3}
            );
        }
    }
}

InsertRestTransContext::InsertRestTransContext() : TransContext(TransContextType::InsertRest) { }

InsertRestTransContext::~InsertRestTransContext() { }

}