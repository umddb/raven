#pragma once

#include "boost/asio/thread_pool.hpp"
#include "postgresql/libpq-fe.h"

#include "catalog/service/catalogtask.h"
#include "catalog/service/txnmgr.h"
#include "catalog/translator/translator.h"
#include "utils/threadpool.h"

namespace ercat {

class CatalogService {
public:
    static void init();
    static CatalogService& getInstance();
    
    ~CatalogService();
    void start();
    void shutDown();
    void enqueueTask(CatalogTask* task, bool ok);
    void handleCatalogTask(CatalogTask* task, bool ok);
    void handleGetTable(CatalogTask* task);
    void handleListFiles(CatalogTask* task);
    void handleAppendFiles(CatalogTask* task);
    void handleRewriteFiles(CatalogTask* task);
    void handleExecQuery(CatalogTask* task);
    void handleStartTransaction(CatalogTask* task);
    void handleCommit(CatalogTask* task);
    void handleExpireSnapshots(CatalogTask* task);

private:
    static CatalogService& getInstanceImpl();
    static int myWorkerId();

    CatalogService();
    void appendFiles(CatalogTask* task, const std::string& values_clause);
    void commitAppendFiles(CatalogTask* task, const std::string& values_clause);
    void rewriteFiles(CatalogTask* task, const std::string& values_to_replace,
            const std::string& values_to_add);
    void commitRewriteFiles(CatalogTask* task, const std::string& values_to_replace,
            const std::string& values_to_add);
    void expireSnapshot(CatalogTask* task);
    void expireSnapshotsOlderThan(CatalogTask* task);
    void expireSnapshotsRetainLast(CatalogTask* task);
    
    
    TransactionManager& txn_mgr_;
    std::unique_ptr<ThreadPool> workers_;
    std::vector<std::unique_ptr<Translator>> translator_;
    std::vector<PGconn*> pg_conn_;
    std::string pg_conn_str_;
    bool shut_down_;

};




}