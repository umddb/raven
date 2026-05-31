#pragma once

#include <array>
#include <memory>
#include <vector>
#include <string>

#include "boost/unordered/unordered_flat_map.hpp"
#include "boost/unordered/unordered_flat_set.hpp"
#include "utils/catcache.h"

namespace ercat {

// Using enum to allow switch statements
enum class TransContextType {
    StmtBlock,
    AlterEntStmt,
    AlterRelStmt,
    CreateEntStmt,
    TableElement,
    CreateRelStmt,
    SelectPramary,
    SelectFileSnapshot,
    SelectFileDelta,
    WithClause,
    CommonTableExpr,
    DeleteRootStmt,
    DeleteRelStmt,
    DeleteFileStmt,
    TargetList,
    TargetList1,
    FromList,
    TransactionStmt,
    InsertRootStmt,
    InsertEntStmt,
    InsertRelStmt,
    InsertFileStmt,
    InsertRest,
    WhereClause
};

// Base traslation context
class TransContext {
public:
    TransContext(TransContextType trans_ctx_type);
    virtual ~TransContext();
    TransContextType transContextType() const;
    virtual size_t startIdx() const;
    virtual size_t endIdx() const;
    virtual const std::vector<std::unique_ptr<TransContext>>& children() const;
    virtual void setStartIdx(size_t start_idx);
    virtual void setEndIdx(size_t end_idx);
    virtual void addChild(TransContext * child_ctx);
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const;

    TransContextType trans_ctx_type_;
    size_t start_idx_;
    size_t end_idx_;
    std::vector<std::unique_ptr<TransContext>> children_;
};

// stmt_block
class StmtBlockTransContext : public TransContext {
public:
    StmtBlockTransContext();
    ~StmtBlockTransContext();
};

// alterentstmt
class AlterEntStmtTransContext : public TransContext {
public:    
    AlterEntStmtTransContext();
    ~AlterEntStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    std::string ent_name_;
    std::string constraint_name_;
};

// alterrelstmt
class AlterRelStmtTransContext : public TransContext {
public:    
    AlterRelStmtTransContext();
    ~AlterRelStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    std::string rel_name_;
    std::string src_ent_;
    std::string dest_ent_;
    std::string constraint_name_;
    std::string referrer_ent_;
    std::string referee_ent_;
    std::string retention_period_;

private:
    enum class VisitState {
        UNVISITED = 0, // Default state
        VISITING,
        VISITED
    };

    bool createsCycle(RelId new_rel_id, bool right_ref, bool left_ref) const;
    bool hasCycleDFS(const EntId node, 
                 const boost::unordered_flat_map<EntId, boost::unordered_flat_set<EntId>>& graph, 
                 boost::unordered_flat_map<EntId, VisitState>& states) const;
};

// createentstmt
class CreateEntStmtTransContext : public TransContext {
public:    
    CreateEntStmtTransContext();
    ~CreateEntStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    bool is_abstract_;
    bool is_file_list_;
    bool is_root_;
    std::string ent_name_;
    std::vector<std::string> implements_;
};

// tableelementlist
class TableElementTransContext : public TransContext {
public:    
    TableElementTransContext();
    ~TableElementTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
};


// createrelstmt
class CreateRelStmtTransContext : public TransContext {
public:    
    CreateRelStmtTransContext();
    ~CreateRelStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    std::string rel_name_;
    std::string src_ent_;
    std::string dest_ent_;
};

class DeleteRootStmtTransContext : public TransContext {
public:    
    DeleteRootStmtTransContext();
    ~DeleteRootStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    std::string ent_name_;
};

class DeleteRelStmtTransContext : public TransContext {
public:    
    DeleteRelStmtTransContext();
    ~DeleteRelStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    std::string rel_name_;
    std::string src_ent_;
    std::string dest_ent_;
    std::string retention_;
};

class DeleteFileStmtTransContext : public TransContext {
public:
    DeleteFileStmtTransContext();
    ~DeleteFileStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    
    bool prepare_;
    std::string ent_name_;
    ObjId obj_id_;
    VersionId vid_;
    std::pair<size_t, size_t> values_clause_;
};


class WhereClauseTransContext : public TransContext {
public:
    WhereClauseTransContext();
    ~WhereClauseTransContext();
};

// select_pramary
class SelectPramaryTransContext : public TransContext {
public:    
    SelectPramaryTransContext();
    ~SelectPramaryTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
};

class SelectFileDeltaStmtTransContext : public TransContext {
public:
    SelectFileDeltaStmtTransContext();
    ~SelectFileDeltaStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    std::string ent_name_;
    ObjId obj_id_;
    VersionId start_vid_;
    VersionId end_vid_;
};

class SelectFileSnapshotStmtTransContext : public TransContext {
public:
    SelectFileSnapshotStmtTransContext();
    ~SelectFileSnapshotStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    std::string ent_name_;
    ObjId obj_id_;
    VersionId vid_;
};

class WithClauseTransContext : public TransContext {
public:
    WithClauseTransContext();
    ~WithClauseTransContext();
};

class CommonTableExprTransContext : public TransContext {
public:
    CommonTableExprTransContext();
    ~CommonTableExprTransContext();

    std::string name_;
};

// target_list_ (terminal/leaf context)
class TargetListTransContext : public TransContext {
public:    
    TargetListTransContext();
    ~TargetListTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    // translated target list
    std::string target_list_trans_;
};

class TargetList1TransContext : public TransContext {
public:    
    TargetList1TransContext();
    ~TargetList1TransContext();
};

// from_list2 (terminal/leaf context)
class FromListTransContext : public TransContext {
public:    
    FromListTransContext();
    ~FromListTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;

    // entity variable binding that maps entity variable name to entity type name
    boost::unordered_flat_map<std::string, std::string> entity_binding_;
    // relation "applications" (like function applications) on the entity variables 
    // consisting of (relation name, entity1 name, entity2 name) tuples
    std::vector<std::array<std::string, 3>> rel_app_;
};

class TransactionStmtTransContext : public TransContext {
public:
    TransactionStmtTransContext();
    ~TransactionStmtTransContext();
};

class InsertRootStmtTransContext : public TransContext {
public:
    InsertRootStmtTransContext();
    ~InsertRootStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    std::string ent_name_;
};

class InsertEntStmtTransContext : public TransContext {
public:
    InsertEntStmtTransContext();
    ~InsertEntStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    std::string ent_name_;
};

class InsertRelStmtTransContext : public TransContext {
public:
    InsertRelStmtTransContext();
    ~InsertRelStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    std::string rel_name_;
    std::string src_ent_;
    std::string dest_ent_;
};

class InsertFileStmtTransContext : public TransContext {
public:
    InsertFileStmtTransContext();
    ~InsertFileStmtTransContext();
    virtual void translate(const std::string& input_string,
            std::vector<std::string>& output_streams, std::vector<std::string>& errors) const override;
    
    bool prepare_;
    std::string ent_name_;
    ObjId obj_id_;
    VersionId vid_;
    std::string insert_column_list_;
    std::pair<size_t, size_t> values_clause_;
};

class InsertRestTransContext : public TransContext {
public:
    InsertRestTransContext();
    ~InsertRestTransContext();
};

}