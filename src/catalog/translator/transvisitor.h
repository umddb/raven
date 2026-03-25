#pragma once

#include "catalog/parser/er/ERQLParserBaseVisitor.h"
#include "catalog/translator/transcontext.h"

namespace ercat {

class TransVisitor : public ERQLParserBaseVisitor {
public:
    TransVisitor(std::vector<std::string>& errors);
    ~TransVisitor();
    virtual std::any visitStmtblock(ERQLParser::StmtblockContext* context) override;
    virtual std::any visitAlterentstmt(ERQLParser::AlterentstmtContext* context) override;
    virtual std::any visitAlterrelstmt(ERQLParser::AlterrelstmtContext* context) override;
    virtual std::any visitRelconstraintelem(ERQLParser::RelconstraintelemContext* context) override;
    virtual std::any visitCreateentstmt(ERQLParser::CreateentstmtContext* context) override;
    virtual std::any visitOptimplement(ERQLParser::OptimplementContext* context) override;
    virtual std::any visitTableelementlist(ERQLParser::TableelementlistContext* context) override;
    virtual std::any visitCreaterelstmt(ERQLParser::CreaterelstmtContext* context) override;
    virtual std::any visitDeleterootstmt(ERQLParser::DeleterootstmtContext* context) override;
    virtual std::any visitDeleterelstmt(ERQLParser::DeleterelstmtContext* context) override;
    virtual std::any visitDeletefilestmt(ERQLParser::DeletefilestmtContext* context) override;
    virtual std::any visitWhere_clause(ERQLParser::Where_clauseContext* context) override;
    virtual std::any visitSimple_select_pramary(ERQLParser::Simple_select_pramaryContext* context) override;
    virtual std::any visitSelectfiledeltastmt(ERQLParser::SelectfiledeltastmtContext* context) override;
    virtual std::any visitSelectfilesnapshotstmt(ERQLParser::SelectfilesnapshotstmtContext* context) override;
    virtual std::any visitWith_clause(ERQLParser::With_clauseContext* context) override;   
    virtual std::any visitCommon_table_expr(ERQLParser::Common_table_exprContext* context) override;
    virtual std::any visitTarget_list_(ERQLParser::Target_list_Context* context) override;
    virtual std::any visitTarget_list1(ERQLParser::Target_list1Context* context) override;
    virtual std::any visitFrom_list2(ERQLParser::From_list2Context* context) override;
    virtual std::any visitTransactionstmt(ERQLParser::TransactionstmtContext* context) override;
    virtual std::any visitInsertrootstmt(ERQLParser::InsertrootstmtContext* context) override;
    virtual std::any visitInsertentstmt(ERQLParser::InsertentstmtContext* context) override;
    virtual std::any visitInsertrelstmt(ERQLParser::InsertrelstmtContext* context) override;
    virtual std::any visitInsertfilestmt(ERQLParser::InsertfilestmtContext* context) override;
    virtual std::any visitInsert_rest(ERQLParser::Insert_restContext* context) override;
    TransContext* rootTransContext();

private:
    // basic initialization routine for TransContext object
    void initTransContext(antlr4::ParserRuleContext* rule_ctx, TransContext* trans_ctx);
    // construct children TransContext
    std::any createChildren(antlr4::ParserRuleContext* rule_ctx, TransContext* trans_ctx);

    // Root translation context
    std::unique_ptr<TransContext> root_;
    // Current translation context stack where the current context is at the top
    std::stack<TransContext*> cur_trans_ctx_;
    // Errors output during TransContext generation.
    std::vector<std::string>& errors_;
};

}