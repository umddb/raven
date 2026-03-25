#include "boost/asio/post.hpp"

#include "catalog/service/catalogservice.h"
#include "catalog/service/txnmgr.h"
#include "utils/config.h"
#include "utils/pgutil.h"

namespace ercat {

void CatalogService::init() {
    getInstanceImpl();
}

CatalogService& CatalogService::getInstance() {
    return getInstanceImpl();
}

CatalogService::~CatalogService() {
    shutDown();
}

void CatalogService::start() {
    int num_workers = workers_->num_workers_;
    for (int i = 0; i < num_workers; i++) {
        translator_.push_back(std::make_unique<Translator>());
        pg_conn_.push_back(PGUtil::connectPG(pg_conn_str_));
    }
}

void CatalogService::shutDown() {
    if (!shut_down_) {
        workers_->shutDown();
        for (auto pg_conn : pg_conn_) {
            PQfinish(pg_conn);
        }
        shut_down_ = true;
    }
}

void CatalogService::enqueueTask(CatalogTask* task, bool ok) {
    boost::asio::post(workers_->workers_, [this, task, ok](){ 
        handleCatalogTask(task, ok);     
    });
}

void CatalogService::handleCatalogTask(CatalogTask* task, bool ok) {
    task->times_++;

    switch(task->type_) {
        case CatalogTaskType::GET_TABLE:
            handleGetTable(task);
            break;
        case CatalogTaskType::LIST_FILES:
            handleListFiles(task);
            break;
        case CatalogTaskType::APPEND_FILES:
            handleAppendFiles(task);
            break;
        case CatalogTaskType::REWRITE_FILES:
            handleRewriteFiles(task);
            break;
        case CatalogTaskType::EXEC_QUERY:
            handleExecQuery(task);
            break;
        case CatalogTaskType::START_TRANSACTION:
            handleStartTransaction(task);
            break;
        case CatalogTaskType::COMMIT:
            handleCommit(task);
            break;
        case CatalogTaskType::EXPIRE_SNAPSHOTS:
            handleExpireSnapshots(task);
            break;
        default:
            break;
    }    
}

void CatalogService::handleGetTable(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    GetTableRequest * get_table_request = static_cast<GetTableRequest*>(task->request_);
    GetTableResponse * get_table_response = static_cast<GetTableResponse*>(task->response_);

    std::string input_query;
    if (get_table_request->has_vid()) {
        fmt::format_to(
            std::back_inserter(input_query),
            "SELECT t.obj_id, ts.obj_id, ts.vid, ts.create_time, ts.schema, ts.table_type "
            "FROM Contains(Workspace w, Database d), Contains(d, Table t), Contains(t, TableSnapshot ts) "
            "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND t.table_name = '{2}' AND ts.vid = {3}",
            get_table_request->workspace_name(), get_table_request->db_name(), get_table_request->table_name(),
            get_table_request->vid()
        );
    }
    else {
        fmt::format_to(
            std::back_inserter(input_query),
            "SELECT t.obj_id, ts.obj_id, ts.vid, ts.create_time, ts.schema, ts.table_type "
            "FROM Contains(Workspace w, Database d), Contains(d, Table t), Head(t, TableSnapshot ts) "
            "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND t.table_name = '{2}'",
            get_table_request->workspace_name(), get_table_request->db_name(), get_table_request->table_name()
        );
    }
    
    std::string final_query = translator->translate(input_query);
    bool res_valid = false;
    int retry = 0;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int vid_fnum = PQfnumber(res, "vid");
                int create_time_fnum = PQfnumber(res, "create_time");
                int schema_fnum = PQfnumber(res, "schema");
                int table_type_fnum = PQfnumber(res, "table_type");
                if (PQntuples(res) == 1) {
                    get_table_response->set_success(true);
                    TableObject* table = get_table_response->mutable_table();
                    table->set_workspace_name(get_table_request->workspace_name());
                    table->set_db_name(get_table_request->db_name());
                    table->set_table_name(get_table_request->table_name());
                    table->set_table_obj_id(std::stoll(PQgetvalue(res, 0, 0)));
                    table->set_snapshot_obj_id(std::stoll(PQgetvalue(res, 0, 1)));
                    table->set_snapshot_vid(std::stol(PQgetvalue(res, 0, vid_fnum)));
                    table->set_create_time(std::string(PQgetvalue(res, 0, create_time_fnum)));
                    table->set_schema(std::string(PQgetvalue(res, 0, schema_fnum)));
                    table->set_table_type(std::string(PQgetvalue(res, 0, table_type_fnum)));
                }
                else {
                    get_table_response->set_success(false);
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    if (!res_valid) {
        get_table_response->set_success(false);
    }

    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<GetTableResponse>*>
            (task->writer_)->Finish(*get_table_response, grpc::Status::OK, task);
}

void CatalogService::handleListFiles(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    ListFilesRequest* list_files_request = static_cast<ListFilesRequest*>(task->request_);
    ListFilesResponse* list_files_response = static_cast<ListFilesResponse*>(task->response_);

    ObjId obj_id = list_files_request->snapshot_obj_id();
    VersionId vid = list_files_request->snapshot_vid();
    std::string input_query;
    fmt::format_to(
        std::back_inserter(input_query),
        "SELECT file_path, format, file_size, modification_time, tag FROM FILELIST SNAPSHOT TableSnapshot({0}, {1})",
        obj_id, vid
    );

    std::string final_query = translator->translate(input_query);
    bool res_valid = false;
    int retry = 0;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int file_path_fnum = PQfnumber(res, "file_path");
                int format_fnum = PQfnumber(res, "format");
                int file_size_fnum = PQfnumber(res, "file_size");
                int modification_time_fnum = PQfnumber(res, "modification_time");
                int tag_fnum = PQfnumber(res, "tag");
                for (int i = 0; i < PQntuples(res); i++) {
                    FileObject* file = list_files_response->add_files();
                    file->set_path(std::string(PQgetvalue(res, i, file_path_fnum)));
                    if (!PQgetisnull(res, i, format_fnum)) {
                        file->set_format(std::string(PQgetvalue(res, i, format_fnum)));
                    }
                    if (!PQgetisnull(res, i, file_size_fnum)) {
                        file->set_size(std::stol(PQgetvalue(res, i, file_size_fnum)));
                    }
                    if (!PQgetisnull(res, i, modification_time_fnum)) {
                        file->set_modification_time(std::stoll(PQgetvalue(res, i, modification_time_fnum)));
                    }
                    if (!PQgetisnull(res, i, tag_fnum)) {
                        file->set_format(std::string(PQgetvalue(res, i, tag_fnum)));
                    }
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    list_files_response->set_success(res_valid);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<ListFilesResponse>*>
            (task->writer_)->Finish(*list_files_response, grpc::Status::OK, task);
}



void CatalogService::handleAppendFiles(CatalogTask* task) {
    AppendFilesRequest* append_files_request = static_cast<AppendFilesRequest*>(task->request_);
    AppendFilesResponse* append_files_response = static_cast<AppendFilesResponse*>(task->response_);

    // no files to append
    if (append_files_request->files_size() == 0) {
        append_files_response->set_success(true);
        task->status_ = CatalogTaskStatus::FINISH;
        static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>
                (task->writer_)->Finish(*append_files_response, grpc::Status::OK, task);
        return;
    }
    
    std::string values_clause;
    values_clause.reserve(append_files_request->files_size()*30);
    for (auto& file : append_files_request->files()) {
        fmt::format_to(
            std::back_inserter(values_clause),
            "('{}',",
            file.path()
        );
        if (file.has_format()) {
            fmt::format_to(
                std::back_inserter(values_clause),
                "'{}',",
                file.format()
            );
        }
        else {
            values_clause.append("NULL::text,");
        }
        if (file.has_size()) {
            fmt::format_to(
                std::back_inserter(values_clause),
                "{},",
                file.size()
            );
        }
        else {
            values_clause.append("NULL::integer,");
        }
        if (file.has_modification_time()) {
            fmt::format_to(
                std::back_inserter(values_clause),
                "{},",
                file.modification_time()
            );
        }
        else {
            values_clause.append("NULL::bigint,");
        }
        if (file.has_tag()) {
            fmt::format_to(
                std::back_inserter(values_clause),
                "'{}'),",
                file.tag()
            );
        }
        else {
            values_clause.append("NULL::text),");
        }
    }
    // erase the last comma
    values_clause.pop_back();

    if (append_files_request->has_txn_id()) {
        appendFiles(task, values_clause);
    }
    else {
        commitAppendFiles(task, values_clause);
    }
}

void CatalogService::appendFiles(CatalogTask* task, const std::string& values_clause) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    AppendFilesRequest* append_files_request = static_cast<AppendFilesRequest*>(task->request_);
    AppendFilesResponse* append_files_response = static_cast<AppendFilesResponse*>(task->response_);

    Transaction* txn = txn_mgr_.getTransaction(append_files_request->txn_id());

    if (txn == nullptr || !txn->version_map_.contains(append_files_request->snapshot_obj_id())) {
        append_files_response->set_success(false);
        task->status_ = CatalogTaskStatus::FINISH;
        static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>
                (task->writer_)->Finish(*append_files_response, grpc::Status::OK, task);
        return;
    }

    ObjId snapshot_obj_id = append_files_request->snapshot_obj_id();
    VersionId snapshot_vid = txn->version_map_.at(snapshot_obj_id);

    std::string& input_command = txn->command_str_;
    if (append_files_request->prepare()) {
        fmt::format_to(
            std::back_inserter(input_command),
            "PREPARE INSERT INTO FILELIST TableSnapshot({0}, {1}) (file_path, format, file_size, modification_time, tag) "
                "VALUES {2};",
            snapshot_obj_id, snapshot_vid, values_clause
        );
    }
    else {
        fmt::format_to(
            std::back_inserter(input_command),
            "INSERT INTO FILELIST TableSnapshot({0}, {1}) (file_path, format, file_size, modification_time, tag) "
                "VALUES {2};"
            "DELETE FROM RELATIONSHIP Head(Table, TableSnapshot) WHERE src_obj_id = {3} AND src_vid = 0 "
                "AND dest_obj_id = {0} AND dest_vid ={1};"
            "INSERT INTO RELATIONSHIP Contains(Table, TableSnapshot) VALUES ('Table', {3}, 0, 'TableSnapshot', {0}, {1} + 1);"
            "INSERT INTO RELATIONSHIP Head(Table, TableSnapshot) VALUES ('Table', {3}, 0, 'TableSnapshot', {0}, {1} + 1);",
            snapshot_obj_id, snapshot_vid, values_clause, append_files_request->table_obj_id()
        );
    }

    // complete grpc call
    append_files_response->set_success(true);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>
            (task->writer_)->Finish(*append_files_response, grpc::Status::OK, task);
}

void CatalogService::commitAppendFiles(CatalogTask* task, const std::string& values_clause) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    AppendFilesRequest* append_files_request = static_cast<AppendFilesRequest*>(task->request_);
    AppendFilesResponse* append_files_response = static_cast<AppendFilesResponse*>(task->response_);

    ObjId snapshot_obj_id = append_files_request->snapshot_obj_id();
    VersionId snapshot_vid = append_files_request->snapshot_vid();

    std::string input_command;
    fmt::format_to(
        std::back_inserter(input_command),
        "BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;"
        "INSERT INTO FILELIST TableSnapshot({0}, {1}) (file_path, format, file_size, modification_time, tag) "
            "VALUES {2};"
        "DELETE FROM RELATIONSHIP Head(Table, TableSnapshot) WHERE src_obj_id = {3} AND src_vid = 0 "
            "AND dest_obj_id = {0} AND dest_vid ={1};"
        "INSERT INTO RELATIONSHIP Contains(Table, TableSnapshot) VALUES ('Table', {3}, 0, 'TableSnapshot', {0}, {1} + 1);"
        "INSERT INTO RELATIONSHIP Head(Table, TableSnapshot) VALUES ('Table', {3}, 0, 'TableSnapshot', {0}, {1} + 1);"
        "COMMIT;",
        snapshot_obj_id, snapshot_vid, values_clause, append_files_request->table_obj_id()
    );
    
    // translate and execute the query
    std::string output_command = translator->translateMultiDMLStmts(input_command);

    bool success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);
    
    // complete grpc call
    append_files_response->set_success(success);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>
            (task->writer_)->Finish(*append_files_response, grpc::Status::OK, task);
}  

void CatalogService::handleRewriteFiles(CatalogTask* task) {
    RewriteFilesRequest* rewrite_files_request = static_cast<RewriteFilesRequest*>(task->request_);
    RewriteFilesResponse* rewrite_files_response = static_cast<RewriteFilesResponse*>(task->response_);
    
    std::string values_to_replace;
    values_to_replace.reserve(rewrite_files_request->files_to_replace_size() * 10);
    for (auto& file : rewrite_files_request->files_to_replace()) {
        fmt::format_to(
            std::back_inserter(values_to_replace),
            "('{}'),",
            file
        );
    }

    // remove the last comma
    if (!values_to_replace.empty()) {
        values_to_replace.pop_back();
    }

    std::string values_to_add;
    values_to_add.reserve(rewrite_files_request->files_to_add_size() * 30);
    for (auto& file : rewrite_files_request->files_to_add()) {
        fmt::format_to(
            std::back_inserter(values_to_add),
            "('{}',",
            file.path()
        );
        if (file.has_format()) {
            fmt::format_to(
                std::back_inserter(values_to_add),
                "'{}',",
                file.format()
            );
        }
        else {
            values_to_add.append("NULL::text,");
        }
        if (file.has_size()) {
            fmt::format_to(
                std::back_inserter(values_to_add),
                "{},",
                file.size()
            );
        }
        else {
            values_to_add.append("NULL::integer,");
        }
        if (file.has_modification_time()) {
            fmt::format_to(
                std::back_inserter(values_to_add),
                "{},",
                file.modification_time()
            );
        }
        else {
            values_to_add.append("NULL::bigint,");
        }
        if (file.has_tag()) {
            fmt::format_to(
                std::back_inserter(values_to_add),
                "'{}'),",
                file.tag()
            );
        }
        else {
            values_to_add.append("NULL::text),");
        }
    }
    // remove the last comma
    if (!values_to_add.empty()) {
        values_to_add.pop_back();
    }

    if (rewrite_files_request->has_txn_id()) {
        rewriteFiles(task, values_to_replace, values_to_add);
    }
    else {
        commitRewriteFiles(task, values_to_replace, values_to_add);
    }

}

void CatalogService::rewriteFiles(CatalogTask* task, const std::string& values_to_replace,
            const std::string& values_to_add) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    RewriteFilesRequest* rewrite_files_request = static_cast<RewriteFilesRequest*>(task->request_);
    RewriteFilesResponse* rewrite_files_response = static_cast<RewriteFilesResponse*>(task->response_);

    Transaction* txn = txn_mgr_.getTransaction(rewrite_files_request->txn_id());
    if (txn == nullptr || !txn->version_map_.contains(rewrite_files_request->snapshot_obj_id())) {
        rewrite_files_response->set_success(false);
        task->status_ = CatalogTaskStatus::FINISH;
        static_cast<grpc::ServerAsyncResponseWriter<RewriteFilesResponse>*>
                (task->writer_)->Finish(*rewrite_files_response, grpc::Status::OK, task);
        return;
    }

    ObjId snapshot_obj_id = rewrite_files_request->snapshot_obj_id();
    VersionId snapshot_vid = txn->version_map_.at(snapshot_obj_id);

    std::string& input_command = txn->command_str_;
    if (rewrite_files_request->prepare()) {
        if (!values_to_replace.empty()) {
            fmt::format_to(
                std::back_inserter(input_command),
                "PREPARE DELETE FROM FILELIST TableSnapshot({0}, {1}) VALUES {2};",
                snapshot_obj_id, snapshot_vid, values_to_replace
            );
        }

        if (!values_to_add.empty()) {
            fmt::format_to(
                std::back_inserter(input_command),
                "PREPARE INSERT INTO FILELIST TableSnapshot({0}, {1}) "
                "(file_path, format, file_size, modification_time, tag) "
                    "VALUES {2};",
                snapshot_obj_id, snapshot_vid, values_to_add
            );
        }
    }
    else {
        // if both are not empty, one has to be a prepare statement
        if (!values_to_replace.empty() && !values_to_add.empty()) {
            input_command.append("PREPARE ");
        }

        if (!values_to_replace.empty()) {
            fmt::format_to(
                std::back_inserter(input_command),
                "DELETE FROM FILELIST TableSnapshot({0}, {1}) VALUES {2};",
                snapshot_obj_id, snapshot_vid, values_to_replace
            );
        }

        if (!values_to_add.empty()) {
            fmt::format_to(
                std::back_inserter(input_command),
                "INSERT INTO FILELIST TableSnapshot({0}, {1}) "
                "(file_path, format, file_size, modification_time, tag) "
                    "VALUES {2};",
                snapshot_obj_id, snapshot_vid, values_to_add
            );
        }

        fmt::format_to(
            std::back_inserter(input_command),
            "DELETE FROM RELATIONSHIP Head(Table, TableSnapshot) WHERE src_obj_id = {2} AND src_vid = 0 "
                "AND dest_obj_id = {0} AND dest_vid = {1};"
            "INSERT INTO RELATIONSHIP Contains(Table, TableSnapshot) VALUES "
                "('Table', {2}, 0, 'TableSnapshot', {0}, {1} + 1);"
            "INSERT INTO RELATIONSHIP Head(Table, TableSnapshot) VALUES "
                "('Table', {2}, 0, 'TableSnapshot', {0}, {1} + 1);",
            snapshot_obj_id, snapshot_vid,
            rewrite_files_request->table_obj_id()
        );
    }

    rewrite_files_response->set_success(true);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<RewriteFilesResponse>*>
            (task->writer_)->Finish(*rewrite_files_response, grpc::Status::OK, task);
}

void CatalogService::commitRewriteFiles(CatalogTask* task, const std::string& values_to_replace,
            const std::string& values_to_add) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    RewriteFilesRequest* rewrite_files_request = static_cast<RewriteFilesRequest*>(task->request_);
    RewriteFilesResponse* rewrite_files_response = static_cast<RewriteFilesResponse*>(task->response_);

    ObjId snapshot_obj_id = rewrite_files_request->snapshot_obj_id();
    VersionId snapshot_vid = rewrite_files_request->snapshot_vid();

    std::string input_command("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;");
    // if both are not empty, one has to be a prepare statement
    if (!values_to_replace.empty() && !values_to_add.empty()) {
        input_command.append("PREPARE ");
    }

    if (!values_to_replace.empty()) {
        fmt::format_to(
            std::back_inserter(input_command),
            "DELETE FROM FILELIST TableSnapshot({0}, {1}) VALUES {2};",
            snapshot_obj_id, snapshot_vid, values_to_replace
        );
    }

    if (!values_to_add.empty()) {
        fmt::format_to(
            std::back_inserter(input_command),
            "INSERT INTO FILELIST TableSnapshot({0}, {1}) (file_path, format, file_size, modification_time, tag) "
                "VALUES {2};",
            snapshot_obj_id, snapshot_vid, values_to_add
        );
    }

    fmt::format_to(
        std::back_inserter(input_command),
        "DELETE FROM RELATIONSHIP Head(Table, TableSnapshot) WHERE src_obj_id = {2}  AND src_vid = 0 "
            "AND dest_obj_id = {0} AND dest_vid ={1};"
        "INSERT INTO RELATIONSHIP Contains(Table, TableSnapshot) VALUES "
            "('Table', {2}, 0, 'TableSnapshot', {0}, {1} + 1);"
        "INSERT INTO RELATIONSHIP Head(Table, TableSnapshot) VALUES "
            "('Table', {2}, 0, 'TableSnapshot', {0}, {1} + 1);"
        "COMMIT;",
        snapshot_obj_id, snapshot_vid, rewrite_files_request->table_obj_id()
    );

    // translate and execute the query
    std::string output_command = translator->translateMultiDMLStmts(input_command);

    bool success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);
    
    // complete grpc call
    rewrite_files_response->set_success(success);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<RewriteFilesResponse>*>
            (task->writer_)->Finish(*rewrite_files_response, grpc::Status::OK, task);
}

void CatalogService::handleExecQuery(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    ExecQueryRequest* exec_query_request = static_cast<ExecQueryRequest*>(task->request_);
    ExecQueryResponse* exec_query_response = static_cast<ExecQueryResponse*>(task->response_);

    std::string final_query = translator->translate(exec_query_request->query());
    bool res_valid = false;
    int retry = 0;
    std::string res_buf;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            // very simple binary encoding
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int num_rows = PQntuples(res);
                int num_columns = PQnfields(res);
                res_buf.reserve(num_rows * num_columns * 8);
                for (int i = 0; i < num_rows; i++) {
                    for (int j = 0; j < num_columns; j++) {
                        if (PQgetisnull(res, i, j)) {
                            uint32_t length_encoding = htole32(static_cast<uint32_t>(-1));
                            res_buf.append(reinterpret_cast<char*>(&length_encoding), sizeof(length_encoding));
                        }
                        else {
                            int length = PQgetlength(res, i, j);
                            uint32_t length_encoding = htole32(static_cast<uint32_t>(length));
                            res_buf.append(reinterpret_cast<char*>(&length_encoding), sizeof(length_encoding));
                            res_buf.append(PQgetvalue(res, i, j), length);
                        }
                    }
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    exec_query_response->set_success(res_valid);
    exec_query_response->set_result_set(res_buf);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<ExecQueryResponse>*>
            (task->writer_)->Finish(*exec_query_response, grpc::Status::OK, task);

}

void CatalogService::handleStartTransaction(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    StartTransactionRequest* start_txn_request = static_cast<StartTransactionRequest*>(task->request_);
    StartTransactionResponse* start_txn_response = static_cast<StartTransactionResponse*>(task->response_);

    // if list of tables is empty, return false
    if (start_txn_request->table_names_size() == 0) {
        start_txn_response->set_success(false);
        task->status_ = CatalogTaskStatus::FINISH;
        static_cast<grpc::ServerAsyncResponseWriter<StartTransactionResponse>*>
                (task->writer_)->Finish(*start_txn_response, grpc::Status::OK, task);
        return;
    }

    Transaction* txn = txn_mgr_.newTransaction();
    start_txn_response->set_txn_id(txn->txn_id_);

    std::string table_names_str = "("; 
    for (auto& table_name : start_txn_request->table_names()) {
        fmt::format_to(
            std::back_inserter(table_names_str),
            " t.table_name = '{0}' OR ",
            table_name
        );
    }

    table_names_str.erase(table_names_str.length() - 3);
    table_names_str.append(")");

    std::string input_query;
    fmt::format_to(
        std::back_inserter(input_query),
        "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;"
        "SELECT t.table_name, t.obj_id, ts.obj_id, ts.vid, ts.create_time, ts.schema, ts.table_type "
        "FROM Contains(Workspace w, Database d), Contains(d, Table t), Head(t, TableSnapshot ts) "
        "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND {2};"
        "COMMIT;",
        start_txn_request->workspace_name(), start_txn_request->db_name(), table_names_str
    );

    std::string final_query = translator->translateMultiDMLStmts(input_query);

    bool res_valid = false;
    int retry = 0;
    bool pg_error = false;
    std::string res_buf;
    while (!res_valid && retry < 5) {
        if (PQstatus(pg_conn) == CONNECTION_OK && pg_error) {
            PGresult* rb_res = PQexec(pg_conn, "ROLLBACK;");
            if (PQresultStatus(rb_res) != PGRES_COMMAND_OK) {
                std::cerr << "Rollback failed: " << PQresultErrorMessage(rb_res) << "\n";
                PQclear(rb_res);

                while ((rb_res = PQgetResult(pg_conn)) != NULL) {
                    PQclear(rb_res);
                }
                
                PQreset(pg_conn);
            }
            else {
                PQclear(rb_res);
                while ((rb_res = PQgetResult(pg_conn)) != NULL) {
                    PQclear(rb_res);
                }
            }

            pg_error = false;
        }
        // in case there was a failure from previous iteration
        start_txn_response->clear_tables();
        txn->version_map_.clear();
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
             // begin transaction    
            PGresult* res = PQgetResult(pg_conn);
            if (res == nullptr) {
                pg_error = true;
                continue;
            }

            res_valid = (PQresultStatus(res) == PGRES_COMMAND_OK);
            pg_error = !res_valid;
            PQclear(res);
            
            // select query
            res = PQgetResult(pg_conn);
            if (res == nullptr) {
                pg_error = true;
                continue;
            }
            res_valid = res_valid && (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                if (start_txn_request->table_names_size() != PQntuples(res)) {
                    txn_mgr_.removeTransaction(txn);
                    start_txn_response->set_success(false);
                    task->status_ = CatalogTaskStatus::FINISH;
                    static_cast<grpc::ServerAsyncResponseWriter<StartTransactionResponse>*>
                            (task->writer_)->Finish(*start_txn_response, grpc::Status::OK, task);
                    return;
                }

                int table_name_fnum = PQfnumber(res, "table_name");
                int vid_fnum = PQfnumber(res, "vid");
                int create_time_fnum = PQfnumber(res, "create_time");
                int schema_fnum = PQfnumber(res, "schema");
                int table_type_fnum = PQfnumber(res, "table_type");
                for (int i = 0; i < PQntuples(res); i++) {
                    // add table to the response
                    TableObject* table = start_txn_response->add_tables();
                    table->set_workspace_name(start_txn_request->workspace_name());
                    table->set_db_name(start_txn_request->db_name());
                    table->set_table_name(std::string(PQgetvalue(res, i, table_name_fnum)));
                    table->set_table_obj_id(std::stoll(PQgetvalue(res, i, 1)));
                    table->set_snapshot_obj_id(std::stoll(PQgetvalue(res, i, 2)));
                    table->set_snapshot_vid(std::stol(PQgetvalue(res, i, vid_fnum)));
                    table->set_create_time(std::string(PQgetvalue(res, i, create_time_fnum)));
                    table->set_schema(std::string(PQgetvalue(res, i, schema_fnum)));
                    table->set_table_type(std::string(PQgetvalue(res, i, table_type_fnum)));
                    
                    // update the transaction version map
                    txn->version_map_.emplace(table->snapshot_obj_id(), table->snapshot_vid());
                }
            }
            else {
                pg_error = true;
            }
            PQclear(res);

            // commit
            res = PQgetResult(pg_conn);
            if (res == nullptr) {
                pg_error = true;
                continue;
            }
            res_valid = res_valid && (PQresultStatus(res) == PGRES_COMMAND_OK);
            pg_error = !res_valid;
            PQclear(res);

            // clear out the rest of result set
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    if (res_valid) {
        txn->command_str_ = "BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;";
    }
    else {
        txn_mgr_.removeTransaction(txn);
    }

    start_txn_response->set_success(res_valid);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<StartTransactionResponse>*>
            (task->writer_)->Finish(*start_txn_response, grpc::Status::OK, task);
}

void CatalogService::handleCommit(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    CommitRequest* commit_request = static_cast<CommitRequest*>(task->request_);
    CommitResponse* commit_response = static_cast<CommitResponse*>(task->response_);

    Transaction* txn = txn_mgr_.getTransaction(commit_request->txn_id());

    if (txn == nullptr) {
        commit_response->set_success(false);
        task->status_ = CatalogTaskStatus::FINISH;
        static_cast<grpc::ServerAsyncResponseWriter<CommitResponse>*>
                (task->writer_)->Finish(*commit_response, grpc::Status::OK, task);
        return;
    }

    txn->command_str_.append("COMMIT;");

    // translate and execute the query
    std::string output_command = translator->translateMultiDMLStmts(txn->command_str_);
    bool success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);

    // complete grpc call
    commit_response->set_success(success);
    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<CommitResponse>*>
            (task->writer_)->Finish(*commit_response, grpc::Status::OK, task);
    
    // remove the transaction
    txn_mgr_.removeTransaction(txn);
}

void CatalogService::handleExpireSnapshots(CatalogTask* task) {
    ExpireSnapshotsRequest* expire_snapshots_request = static_cast<ExpireSnapshotsRequest*>(task->request_);
    ExpireSnapshotsResponse* expire_snapshots_response = static_cast<ExpireSnapshotsResponse*>(task->response_);

    switch (expire_snapshots_request->expire_case()) {
        case ExpireSnapshotsRequest::kVid:
            expireSnapshot(task);
            break;
        case ExpireSnapshotsRequest::kOlderThanVid:
            expireSnapshotsOlderThan(task);
            break;
        case ExpireSnapshotsRequest::kRetainLast:
            expireSnapshotsRetainLast(task);
            break;
        default:
            break;
    }

    task->status_ = CatalogTaskStatus::FINISH;
    static_cast<grpc::ServerAsyncResponseWriter<ExpireSnapshotsResponse>*>
            (task->writer_)->Finish(*expire_snapshots_response, grpc::Status::OK, task);

}

void CatalogService::expireSnapshot(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    ExpireSnapshotsRequest* expire_snapshots_request = static_cast<ExpireSnapshotsRequest*>(task->request_);
    ExpireSnapshotsResponse* expire_snapshots_response = static_cast<ExpireSnapshotsResponse*>(task->response_);

    bool success = false;

    std::string input_query; 
    fmt::format_to(
        std::back_inserter(input_query),
        "SELECT t.obj_id"
        "FROM Contains(Workspace w, Database d), Contains(d, Table t) "
        "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND t.table_name = '{2}'",
        expire_snapshots_request->workspace_name(), expire_snapshots_request->db_name(),
        expire_snapshots_request->table_name()
    );

    std::string final_query = translator->translate(input_query);
    
    bool res_valid = false;
    int64_t table_obj_id = -1;
    int retry = 0;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int obj_idfnum = PQfnumber(res, "obj_id");
                if (PQntuples(res) == 1) {
                    success = true;
                    table_obj_id =  std::stol(PQgetvalue(res, 0, obj_idfnum));
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    if (!success) {
        return;
    }

    std::string input_command;   
    fmt::format_to(
        std::back_inserter(input_command),
        "WITH temp AS (SELECT t.obj_id AS table_obj_id, ts.obj_id AS snapshot_obj_id, ts.vid AS vid "
        "FROM Contains(Table t, TableSnapshot ts) "
        "WHERE t.obj_id = {0} AND t.vid = 0 AND ts.vid = {1}) "
        "DELETE FROM RELATIONSHIP Contains(Table, TableSnapshot) WHERE src_obj_id = temp.table_obj_id "
            "AND src_vid = 0 AND dest_obj_id = temp.snapshot_obj_id AND dest_vid = temp.vid",
        table_obj_id, expire_snapshots_request->vid()
    );
    
    // translate and execute the query
    std::string output_command = translator->translate(input_command);

    success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);
    expire_snapshots_response->set_success(success);

}

void CatalogService::expireSnapshotsOlderThan(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    ExpireSnapshotsRequest* expire_snapshots_request = static_cast<ExpireSnapshotsRequest*>(task->request_);
    ExpireSnapshotsResponse* expire_snapshots_response = static_cast<ExpireSnapshotsResponse*>(task->response_);

    bool success = false;

    std::string input_query; 
    fmt::format_to(
        std::back_inserter(input_query),
        "SELECT t.obj_id"
        "FROM Contains(Workspace w, Database d), Contains(d, Table t) "
        "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND t.table_name = '{2}'",
        expire_snapshots_request->workspace_name(), expire_snapshots_request->db_name(),
        expire_snapshots_request->table_name()
    );

    std::string final_query = translator->translate(input_query);
    
    bool res_valid = false;
    int64_t table_obj_id = -1;
    int retry = 0;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int obj_idfnum = PQfnumber(res, "obj_id");
                if (PQntuples(res) == 1) {
                    success = true;
                    table_obj_id =  std::stol(PQgetvalue(res, 0, obj_idfnum));
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    if (!success) {
        return;
    }

    std::string input_command;   
    fmt::format_to(
        std::back_inserter(input_command),
        "WITH temp AS (SELECT t.obj_id AS table_obj_id, ts.obj_id AS snapshot_obj_id, ts.vid AS vid "
        "FROM Contains(Table t, TableSnapshot ts) "
        "WHERE t.obj_id = {0} AND t.vid = 0 AND ts.vid < {1}) "
        "DELETE FROM RELATIONSHIP Contains(Table, TableSnapshot) WHERE src_obj_id = temp.table_obj_id "
            "AND src_vid = 0 AND dest_obj_id = temp.snapshot_obj_id AND dest_vid = temp.vid",
        table_obj_id, expire_snapshots_request->older_than_vid()
    );
    
    // translate and execute the query
    std::string output_command = translator->translate(input_command);

    success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);
    expire_snapshots_response->set_success(success);

}

void CatalogService::expireSnapshotsRetainLast(CatalogTask* task) {
    int worker_id = myWorkerId();
    Translator* translator = translator_[worker_id].get();
    PGconn* pg_conn = pg_conn_[worker_id];

    ExpireSnapshotsRequest* expire_snapshots_request = static_cast<ExpireSnapshotsRequest*>(task->request_);
    ExpireSnapshotsResponse* expire_snapshots_response = static_cast<ExpireSnapshotsResponse*>(task->response_);

    bool success = false;

    VersionId head_vid;
    std::string input_query; 
    fmt::format_to(
        std::back_inserter(input_query),
        "SELECT t.obj_id, ts.vid "
        "FROM Contains(Workspace w, Database d), Contains(d, Table t), Head(t, TableSnapshot ts) "
        "WHERE w.workspace_name = '{0}' AND d.db_name = '{1}' AND t.table_name = '{2}'",
        expire_snapshots_request->workspace_name(), expire_snapshots_request->db_name(),
        expire_snapshots_request->table_name()
    );

    std::string final_query = translator->translate(input_query);
    
    bool res_valid = false;
    int64_t table_obj_id = -1;
    int retry = 0;
    while (!res_valid && retry < 5) {
        if (!PQsendQuery(pg_conn, final_query.c_str())) {
            std::cerr << "Error sending query to the catalog instance\n";
            if (PQstatus(pg_conn) != CONNECTION_OK) {
                std::cerr << "Error: Catalog connection failure.\n";
                PQfinish(pg_conn);
                pg_conn = PQconnectdb(pg_conn_str_.c_str());
                pg_conn_[worker_id] = pg_conn;
            }
            retry++;
        }
        else {
            PGresult* res = PQgetResult(pg_conn);
            res_valid = (PQresultStatus(res) == PGRES_TUPLES_OK);
            if (res_valid) {
                int obj_idfnum = PQfnumber(res, "obj_id");
                int vid_fnum = PQfnumber(res, "vid");
                if (PQntuples(res) == 1) {
                    success = true;
                    table_obj_id =  std::stol(PQgetvalue(res, 0, obj_idfnum));
                    head_vid = std::stol(PQgetvalue(res, 0, vid_fnum));
                }
            }
            PQclear(res);
            while ((res = PQgetResult(pg_conn)) != NULL) {
                PQclear(res);
            }
            break;
        }
    }

    if (!success) {
        return;
    }

    std::string input_command;   
    fmt::format_to(
        std::back_inserter(input_command),
        "WITH temp AS (SELECT t.obj_id AS table_obj_id, ts.obj_id AS snapshot_obj_id, ts.vid AS vid "
        "FROM Contains(Table t, TableSnapshot ts) "
        "WHERE t.obj_id = {0} AND t.vid = 0 AND ts.vid < {1}) "
        "DELETE FROM RELATIONSHIP Contains(Table, TableSnapshot) WHERE src_obj_id = temp.table_obj_id "
            "AND src_vid = 0 AND dest_obj_id = temp.snapshot_obj_id AND dest_vid = temp.vid",
        table_obj_id, head_vid - expire_snapshots_request->retain_last()
    );
    
    // translate and execute the query
    std::string output_command = translator->translate(input_command);
    std::cout << output_command << std::endl;

    success = PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str_, output_command);
    expire_snapshots_response->set_success(success);

}

CatalogService& CatalogService::getInstanceImpl() {
    static CatalogService instance;
    return instance;
}

int CatalogService::myWorkerId() {
    static std::atomic<int> counter = 0;
    // each thread gets a unique ctx id if not initialized
    static thread_local int my_worker_id = counter.fetch_add(1);
    return my_worker_id;
}

CatalogService::CatalogService() : txn_mgr_(TransactionManager::getInstance()), shut_down_(false) {
    const Config& config = Config::getInstance();
    pg_conn_str_ = Config::getPGConnString();
    int num_workers = std::stoi(config.get("catalog.num_workers"));
    workers_ = std::make_unique<ThreadPool>(num_workers);
}

}
