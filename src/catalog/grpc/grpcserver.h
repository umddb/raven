
#pragma once

#include "boost/asio/thread_pool.hpp"
#include <grpcpp/grpcpp.h>

#include "catalog/grpc/catalog.grpc.pb.h"
#include "utils/threadpool.h"

namespace ercat {

class GRPCServer {
public:
    static void init();
    static GRPCServer& getInstance();
    
    ~GRPCServer();
    void start();
    void shutDown();

private:
    static GRPCServer& getInstanceImpl();

    GRPCServer();
    void handleTasks(grpc::ServerCompletionQueue * cq);

    Catalog::AsyncService service_;
    std::unique_ptr<grpc::Server> server_;
    std::vector<std::unique_ptr<grpc::ServerCompletionQueue>> cq_;
    std::unique_ptr<ThreadPool> workers_;
    bool shut_down_;
};

}