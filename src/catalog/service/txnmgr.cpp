#include "catalog/service/txnmgr.h"

namespace ercat {

void TransactionManager::init() {
    getInstanceImpl();
}

TransactionManager& TransactionManager::getInstance() {
    return getInstanceImpl();
}

TransactionManager::~TransactionManager() {
    txn_map_.visit_all([&](auto& x){ delete x.second; });   
}

TransactionManager& TransactionManager::getInstanceImpl() {
    static TransactionManager instance;
    return instance;
}

TransactionManager::TransactionManager() : next_txn_id_(0) { }

Transaction* TransactionManager::newTransaction() {
    TxnId txn_id = next_txn_id_.fetch_add(1);
    Transaction* txn = new Transaction(txn_id);
    txn_map_.emplace(txn_id, txn);
    return txn;
}

Transaction* TransactionManager::getTransaction(TxnId txn_id) {
    Transaction* txn;
    bool found = txn_map_.visit(txn_id, [&](const auto& x){ txn = x.second; });
    if (found) {
        return txn;
    }
    
    return nullptr;
}

void TransactionManager::removeTransaction(TxnId txn_id) {
    Transaction* txn;
    bool found = txn_map_.visit(txn_id, [&](const auto& x){ txn = x.second; });
    if (found) {
        txn_map_.erase(txn_id);
        delete txn;
    }
}

void TransactionManager::removeTransaction(Transaction* txn) {
    txn_map_.erase(txn->txn_id_);
    delete txn;
}

}