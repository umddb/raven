#pragma once 

#include <atomic>
#include <string>

#include "boost/unordered/concurrent_flat_map.hpp"
#include "boost/unordered/unordered_flat_map.hpp"

#include "utils/types.h"

namespace ercat {

struct Transaction {
    Transaction(TxnId txn_id) : txn_id_(txn_id) { }
    ~Transaction() { }

    const TxnId txn_id_;
    boost::unordered_flat_map<ObjId, VersionId> version_map_;
    std::string command_str_;
};

// a lightweight transaction manager for supporting multi-table transactions
class TransactionManager {
public:
    static void init();
    static TransactionManager& getInstance();

    ~TransactionManager();
    Transaction* newTransaction();
    Transaction* getTransaction(TxnId txn_id);
    void removeTransaction(TxnId txn_id);
    void removeTransaction(Transaction* txn);

private:
    static TransactionManager& getInstanceImpl();

    TransactionManager();

    std::atomic<TxnId> next_txn_id_;
    boost::concurrent_flat_map<TxnId, Transaction*> txn_map_;
};

}