#include "boost/asio/post.hpp"

#include "catalog/grpc/grpcserver.h"
#include "catalog/service/catalogservice.h"
#include "catalog/service/catalogtask.h"
#include "utils/config.h"

namespace ercat {

void GRPCServer::init() {
    getInstanceImpl();
}

GRPCServer& GRPCServer::getInstance() {
    return getInstanceImpl();
}

GRPCServer::~GRPCServer() {
   shutDown();
}

void GRPCServer::start() {
    const Config& config = Config::getInstance();
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(config.get("grpc.server_address"), grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    builder.SetMaxReceiveMessageSize(50 * 1024 * 1024);
    // 2 threads per cq
    for (size_t i = 0; i < (workers_->num_workers_ / 2); i++) {
        cq_.push_back(std::unique_ptr<grpc::ServerCompletionQueue>(builder.AddCompletionQueue()));
    }
    server_ = builder.BuildAndStart();
    
    for (auto& elem : cq_) {
        auto cq = elem.get();
        // add every type of client task to completion queue
        for (int i = static_cast<int>(CatalogTaskType::GET_TABLE); i <= 
            static_cast<int>(CatalogTaskType::EXPIRE_SNAPSHOTS); i++) {
            new CatalogTask(&service_, cq, static_cast<CatalogTaskType>(i));
        }
        // 2 threads per completion queue.
        boost::asio::post(workers_->workers_, [ this, cq ](){ handleTasks(cq); });
        boost::asio::post(workers_->workers_, [ this, cq ](){ handleTasks(cq); });
    }
}

void GRPCServer::shutDown() {
    if (!shut_down_) {
        server_->Shutdown();
        for (auto& cq : cq_) {
            cq->Shutdown();
        }
        workers_->shutDown();
        shut_down_ = true;
    }
}

void GRPCServer::handleTasks(grpc::ServerCompletionQueue * cq) {
    void * tag;
    bool ok;
    CatalogService& catalog_service = CatalogService::getInstance();

    while (cq->Next(&tag, &ok)) {
        CatalogTask* catalog_task = static_cast<CatalogTask*>(tag);
        switch (catalog_task->status_) {
            case CatalogTaskStatus::PROCESS:
                if (ok && catalog_task->times_ == 0) {
                    // create new client task of the same type for concurrency
                    new CatalogTask(catalog_task->service_, catalog_task->cq_, catalog_task->type_);
                }
                else if (!ok) {
                    delete catalog_task;
                    continue;
                }
                catalog_service.enqueueTask(catalog_task, ok);
                break;
            case CatalogTaskStatus::FINISH:
                delete catalog_task;
                break;
            default:
                // TODO handle error
                break;
        }
    }
}

GRPCServer& GRPCServer::getInstanceImpl() {
    static GRPCServer instance;
    return instance;
}

GRPCServer::GRPCServer() : shut_down_(false) { 
    const Config& config = Config::getInstance();
    int num_workers = std::stoi(config.get("grpc.num_workers"));

    workers_ = std::make_unique<ThreadPool>(num_workers);
}

}