#include <sstream>

#include "catalog/translator/transvisitor.h"

namespace ercat {

TransVisitor::TransVisitor(std::vector<std::string>& errors) : errors_(errors) { }

TransVisitor::~TransVisitor() { }

std::any TransVisitor::visitStmtblock(ERQLParser::StmtblockContext* context) {
    // stmt_block is the root
    root_ = std::make_unique<StmtBlockTransContext>();
    // set interval idxes
    root_->setStartIdx(context->getStart()->getStartIndex());
    root_->setEndIdx(context->getStop()->getStopIndex());
    // add context to the stack and visit children
    return createChildren(context, root_.get());
}

std::any TransVisitor::visitAlterentstmt(ERQLParser::AlterentstmtContext* context) {
    AlterEntStmtTransContext* trans_ctx = new AlterEntStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    return std::any();
}

std::any TransVisitor::visitAlterrelstmt(ERQLParser::AlterrelstmtContext* context) {
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name();
    AlterRelStmtTransContext* trans_ctx = new AlterRelStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->rel_name_ = qualified_name[0]->getText();
    trans_ctx->src_ent_ = qualified_name[1]->getText();
    trans_ctx->dest_ent_ = qualified_name[2]->getText();
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitRelconstraintelem(ERQLParser::RelconstraintelemContext* context) {
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name();
    AlterRelStmtTransContext* trans_ctx = static_cast<AlterRelStmtTransContext*>(cur_trans_ctx_.top());
    if (qualified_name.empty()) {
        trans_ctx->retention_period_ = context->StringConstant()->getText();
    }
    else {
        trans_ctx->referrer_ent_ = qualified_name[0]->getText();
        trans_ctx->referee_ent_ = qualified_name[1]->getText();
    }

    return std::any();
}

std::any TransVisitor::visitCreateentstmt(ERQLParser::CreateentstmtContext* context) {
    CreateEntStmtTransContext* trans_ctx = new CreateEntStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->is_abstract_ = (context->ABSTRACT() != nullptr);
    trans_ctx->is_file_list_ = (context->FILELIST() != nullptr);
    trans_ctx->is_root_ = (context->ROOT() != nullptr);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    return createChildren(context, trans_ctx);   
}

std::any TransVisitor::visitOptimplement(ERQLParser::OptimplementContext* context) {
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name_list()->qualified_name();
    CreateEntStmtTransContext* trans_ctx = static_cast<CreateEntStmtTransContext*>(cur_trans_ctx_.top());
    for (auto& elem : qualified_name) {
        trans_ctx->implements_.emplace_back(elem->getText());
    }

    return std::any();
}

std::any TransVisitor::visitTableelementlist(ERQLParser::TableelementlistContext* context) {
    TableElementTransContext* trans_ctx = new TableElementTransContext();
    initTransContext(context, trans_ctx);
    return std::any();
}

std::any TransVisitor::visitCreaterelstmt(ERQLParser::CreaterelstmtContext* context) {
    CreateRelStmtTransContext* trans_ctx = new CreateRelStmtTransContext();
    initTransContext(context, trans_ctx);
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name();
    trans_ctx->rel_name_ = qualified_name[0]->getText();
    trans_ctx->src_ent_ = qualified_name[1]->getText();
    trans_ctx->dest_ent_ = qualified_name[2]->getText();
    return std::any();
}

std::any TransVisitor::visitDeleterootstmt(ERQLParser::DeleterootstmtContext* context) {
    DeleteRootStmtTransContext* trans_ctx = new DeleteRootStmtTransContext();
    initTransContext(context, trans_ctx);
    ERQLParser::Qualified_nameContext* qualified_name = context->qualified_name();
    trans_ctx->ent_name_ = qualified_name->getText();
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitDeleterelstmt(ERQLParser::DeleterelstmtContext* context) {
    DeleteRelStmtTransContext* trans_ctx = new DeleteRelStmtTransContext();
    initTransContext(context, trans_ctx);
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name();
    trans_ctx->rel_name_ = qualified_name[0]->getText();
    trans_ctx->src_ent_ = qualified_name[1]->getText();
    trans_ctx->dest_ent_ = qualified_name[2]->getText();
    ERQLParser::OverridingretContext* overriding_ret = context->overridingret();
    if (overriding_ret != nullptr) {
        trans_ctx->retention_ = overriding_ret->StringConstant()->getText();
    }

    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitDeletefilestmt(ERQLParser::DeletefilestmtContext* context) {
    DeleteFileStmtTransContext* trans_ctx = new DeleteFileStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->prepare_ = (context->PREPARE() != nullptr);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    auto ids = context->Integral();
    trans_ctx->obj_id_ = std::stoll(ids[0]->getText());
    trans_ctx->vid_ = std::stol(ids[1]->getText());
    auto* values_clause = context->values_clause();
    trans_ctx->values_clause_ = std::pair<size_t, size_t>(values_clause->getStart()->getStartIndex(), 
            values_clause->getStop()->getStopIndex());
    return std::any();
}

std::any TransVisitor::visitWhere_clause(ERQLParser::Where_clauseContext* context) {
    WhereClauseTransContext* trans_ctx = new WhereClauseTransContext();
    initTransContext(context, trans_ctx);
    return std::any();
}

std::any TransVisitor::visitSimple_select_pramary(ERQLParser::Simple_select_pramaryContext* context) {
    // new ctx for simple select
    SelectPramaryTransContext* trans_ctx = new SelectPramaryTransContext();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitSelectfiledeltastmt(ERQLParser::SelectfiledeltastmtContext* context) {
    SelectFileDeltaStmtTransContext* trans_ctx = new SelectFileDeltaStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    auto ids = context->Integral();
    trans_ctx->obj_id_ = std::stoll(ids[0]->getText());
    trans_ctx->start_vid_ = std::stol(ids[1]->getText());
    trans_ctx->end_vid_ = std::stol(ids[2]->getText());
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitSelectfilesnapshotstmt(ERQLParser::SelectfilesnapshotstmtContext* context) {
    SelectFileSnapshotStmtTransContext* trans_ctx = new SelectFileSnapshotStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    auto ids = context->Integral();
    trans_ctx->obj_id_ = std::stoll(ids[0]->getText());
    trans_ctx->vid_ = std::stol(ids[1]->getText());
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitWith_clause(ERQLParser::With_clauseContext* context) {
    WithClauseTransContext* trans_ctx = new WithClauseTransContext();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitCommon_table_expr(ERQLParser::Common_table_exprContext* context) {
    CommonTableExprTransContext* trans_ctx = new CommonTableExprTransContext();
    trans_ctx->name_ = context->name()->getText();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitTarget_list_(ERQLParser::Target_list_Context* context) {
    // new ctx for target list
    TargetListTransContext* trans_ctx = new TargetListTransContext();
    initTransContext(context, trans_ctx);
    
    // translate the target list
    std::vector<ERQLParser::Target_el2Context*> target_list = context->target_list2()->target_el2();
    std::stringstream trans_ss;
    for (auto& target_el : target_list) {
        // if target is entity, convert to json text
        ERQLParser::Qualified_nameContext* qualified_name = target_el->qualified_name();
        if (qualified_name != nullptr && qualified_name->indirection() == nullptr) {
            trans_ss << "row_to_json(" << qualified_name->getText() << ")";
        }
        else {    
            // Get the interval from the start of the first token to the end of the last
            antlr4::misc::Interval interval(target_el->start->getStartIndex(), target_el->stop->getStopIndex());
            trans_ss << target_el->start->getInputStream()->getText(interval);
        }
        trans_ss << ", ";
    }
    // pop the last ", "
    trans_ctx->target_list_trans_ = trans_ss.str();
    if (!target_list.empty()) {
        trans_ctx->target_list_trans_.resize(trans_ctx->target_list_trans_.size() - 2);
    }

    // No need to visit children
    return std::any();
}

std::any TransVisitor::visitTarget_list1(ERQLParser::Target_list1Context* context) {
    TargetList1TransContext* trans_ctx = new TargetList1TransContext();
    initTransContext(context, trans_ctx);
    return std::any();
}

std::any TransVisitor::visitFrom_list2(ERQLParser::From_list2Context* context) {
    // new ctx for from list
    FromListTransContext* trans_ctx = new FromListTransContext();
    initTransContext(context, trans_ctx);

    std::vector<ERQLParser::Er_exprContext*> er_exprs = context->er_expr();
    for (auto& er_expr : er_exprs) {
        // entity declaration
        if (ERQLParser::Ent_declContext* ent_decl = er_expr->ent_decl()) {
            std::string colid_txt = ent_decl->colid()->getText();
            if (!trans_ctx->entity_binding_.contains(colid_txt)) {
                trans_ctx->entity_binding_.emplace(colid_txt, ent_decl->qualified_name()->getText());
            }
            else {
                errors_.push_back("Multiple entity declarations " + colid_txt);
            }
        }
        // relation application
        else if (ERQLParser::Rel_appContext* rel_app = er_expr->rel_app()) {
            // a little messy, but for performance
            std::vector<std::string> rel_app_txt;
            for (auto* child : rel_app->children) {
                if (auto* qual_name = dynamic_cast<ERQLParser::Qualified_nameContext*>(child)) {
                    rel_app_txt.push_back(qual_name->getText());
                }
                else if (auto* ent_decl = dynamic_cast<ERQLParser::Ent_declContext*>(child)) {
                    rel_app_txt.push_back(ent_decl->colid()->getText());
                    const std::string& colid_txt(rel_app_txt.back());
                    if (!trans_ctx->entity_binding_.contains(colid_txt)) {
                        trans_ctx->entity_binding_.emplace(colid_txt, ent_decl->qualified_name()->getText());
                    }
                    else {
                        errors_.push_back("Multiple entity declarations " + colid_txt);
                    }
                }
                else if (auto* colid = dynamic_cast<ERQLParser::ColidContext*>(child)) {
                    rel_app_txt.push_back(colid->getText());
                    if (!trans_ctx->entity_binding_.contains(rel_app_txt.back())) {
                        errors_.push_back("Entity declaration missing " + rel_app_txt.back());
                    }
                }
            }
            // index bound check, just in case.
            if (rel_app_txt.size() == 3) {
                trans_ctx->rel_app_.emplace_back();
                trans_ctx->rel_app_.back()[0] = rel_app_txt[0];
                trans_ctx->rel_app_.back()[1] = rel_app_txt[1];
                trans_ctx->rel_app_.back()[2] = rel_app_txt[2];
            }
            else {
                errors_.push_back("Syntax error in relation application.");
            }
        }
    }

    // No need to visit children
    return std::any();
}


std::any TransVisitor::visitTransactionstmt(ERQLParser::TransactionstmtContext* context) {
    TransactionStmtTransContext* trans_ctx = new TransactionStmtTransContext();
    initTransContext(context, trans_ctx);
    return std::any();
}

std::any TransVisitor::visitInsertrootstmt(ERQLParser::InsertrootstmtContext* context) {
    InsertRootStmtTransContext* trans_ctx = new InsertRootStmtTransContext();
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitInsertentstmt(ERQLParser::InsertentstmtContext* context) {
    InsertEntStmtTransContext* trans_ctx = new InsertEntStmtTransContext();
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitInsertrelstmt(ERQLParser::InsertrelstmtContext* context) {
    InsertRelStmtTransContext* trans_ctx = new InsertRelStmtTransContext();
    initTransContext(context, trans_ctx);
    std::vector<ERQLParser::Qualified_nameContext*> qualified_name = context->qualified_name();
    trans_ctx->rel_name_ = qualified_name[0]->getText();
    trans_ctx->src_ent_ = qualified_name[1]->getText();
    trans_ctx->dest_ent_ = qualified_name[2]->getText();
    return createChildren(context, trans_ctx);
}

std::any TransVisitor::visitInsertfilestmt(ERQLParser::InsertfilestmtContext* context) {
    InsertFileStmtTransContext* trans_ctx = new InsertFileStmtTransContext();
    initTransContext(context, trans_ctx);
    trans_ctx->ent_name_ = context->qualified_name()->getText();
    auto ids = context->Integral();
    trans_ctx->prepare_ = (context->PREPARE() != nullptr);
    trans_ctx->obj_id_ = std::stoll(ids[0]->getText());
    trans_ctx->vid_ = std::stol(ids[1]->getText());
    trans_ctx->insert_column_list_ = context->insert_column_list()->getText();
    auto* values_clause = context->values_clause();
    trans_ctx->values_clause_ = std::pair<size_t, size_t>(values_clause->getStart()->getStartIndex(), 
            values_clause->getStop()->getStopIndex());
    return std::any();
}

std::any TransVisitor::visitInsert_rest(ERQLParser::Insert_restContext* context) {
    InsertRestTransContext* trans_ctx = new InsertRestTransContext();
    initTransContext(context, trans_ctx);
    return createChildren(context, trans_ctx);
}

TransContext* TransVisitor::rootTransContext() {
    return root_.get();
}

void TransVisitor::initTransContext(antlr4::ParserRuleContext* rule_ctx, TransContext* trans_ctx) {
    // set interval idxes
    trans_ctx->setStartIdx(rule_ctx->getStart()->getStartIndex());
    trans_ctx->setEndIdx(rule_ctx->getStop()->getStopIndex());
    // add the new ctx as a child
    cur_trans_ctx_.top()->addChild(trans_ctx);
}

std::any TransVisitor::createChildren(antlr4::ParserRuleContext* rule_ctx, TransContext* trans_ctx) {
    // add context to the stack and visit children
    cur_trans_ctx_.push(trans_ctx);
    std::any result = visitChildren(rule_ctx);
    cur_trans_ctx_.pop();
    return result;   
}



}