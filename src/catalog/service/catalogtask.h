#pragma once

#include <grpcpp/grpcpp.h>

#include "catalog/grpc/catalog.pb.h"
#include "catalog/grpc/catalog.grpc.pb.h"

namespace ercat {

enum class CatalogTaskType { GET_TABLE, 
                             LIST_FILES,
                             APPEND_FILES,
                             REWRITE_FILES,
                             EXEC_QUERY,
                             START_TRANSACTION,
                             COMMIT,
                             EXPIRE_SNAPSHOTS };

enum class CatalogTaskStatus { PROCESS, 
                               FINISH };

struct CatalogTask {
    Catalog::AsyncService* service_;
    grpc::ServerCompletionQueue* cq_;
    grpc::ServerContext ctx_;
    CatalogTaskType type_;
    CatalogTaskStatus status_;
    void *request_;
    void *response_;
    void *writer_;
    // rpc specific session data in case of streaming service
    void * session_;
    unsigned int times_;
    
    // disable copy constructor
    CatalogTask(const CatalogTask&) = delete;
    
    // disable assign constructor
    CatalogTask& operator=(const CatalogTask&) = delete;
    CatalogTask(Catalog::AsyncService* service, grpc::ServerCompletionQueue* cq, 
               CatalogTaskType type) : service_(service), cq_(cq), type_(type), 
               status_(CatalogTaskStatus::PROCESS), times_(0) {
        // Call different types of request, response constructors depending on the task type
        switch (type_) {
            case CatalogTaskType::GET_TABLE:
                request_ = new GetTableRequest();
                response_ = new GetTableResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<GetTableResponse>(&ctx_);
                service_->RequestGetTable(&ctx_, static_cast<GetTableRequest*>(request_), 
                                          static_cast<grpc::ServerAsyncResponseWriter<GetTableResponse>*>(writer_),
                                          cq_, cq_, this);
                break;
            case CatalogTaskType::LIST_FILES:
                request_ = new ListFilesRequest();
                response_ = new ListFilesResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<ListFilesResponse>(&ctx_);
                service_->RequestListFiles(&ctx_, static_cast<ListFilesRequest*>(request_),
                                          static_cast<grpc::ServerAsyncResponseWriter<ListFilesResponse>*>(writer_),
                                          cq_, cq_, this);
                break;
            case CatalogTaskType::APPEND_FILES:
                request_ = new AppendFilesRequest();
                response_ = new AppendFilesResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<AppendFilesResponse>(&ctx_);
                service_->RequestAppendFiles(&ctx_, static_cast<AppendFilesRequest*>(request_), 
                                       static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>(writer_),
                                       cq_, cq_, this);
                break;
            case CatalogTaskType::REWRITE_FILES:
                request_ = new RewriteFilesRequest();
                response_ = new RewriteFilesResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<RewriteFilesResponse>(&ctx_);
                service_->RequestRewriteFiles(&ctx_, static_cast<RewriteFilesRequest*>(request_), 
                                            static_cast<grpc::ServerAsyncResponseWriter<RewriteFilesResponse>*>(writer_),
                                            cq_, cq_, this);
                break;
            case CatalogTaskType::EXEC_QUERY:
                request_ = new ExecQueryRequest();
                response_ = new ExecQueryResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<ExecQueryResponse>(&ctx_);
                service_->RequestExecQuery(&ctx_, static_cast<ExecQueryRequest*>(request_), 
                                              static_cast<grpc::ServerAsyncResponseWriter<ExecQueryResponse>*>(writer_),
                                              cq_, cq_, this);
                break;
            case CatalogTaskType::START_TRANSACTION:
                request_ = new StartTransactionRequest();
                response_ = new StartTransactionResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<StartTransactionResponse>(&ctx_);
                service_->RequestStartTransaction(&ctx_, static_cast<StartTransactionRequest*>(request_), 
                                            static_cast<grpc::ServerAsyncResponseWriter<StartTransactionResponse>*>(writer_),
                                            cq_, cq_, this);
                break;
            case CatalogTaskType::COMMIT:
                request_ = new CommitRequest();
                response_ = new CommitResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<CommitResponse>(&ctx_);
                service_->RequestCommit(&ctx_, static_cast<CommitRequest*>(request_), 
                                        static_cast<grpc::ServerAsyncResponseWriter<CommitResponse>*>(writer_),
                                        cq_, cq_, this);
                break;
            case CatalogTaskType::EXPIRE_SNAPSHOTS:
                request_ = new ExpireSnapshotsRequest();
                response_ = new ExpireSnapshotsResponse();
                writer_ = new grpc::ServerAsyncResponseWriter<ExpireSnapshotsResponse>(&ctx_);
                service_->RequestExpireSnapshots(&ctx_, static_cast<ExpireSnapshotsRequest*>(request_), 
                                           static_cast<grpc::ServerAsyncResponseWriter<ExpireSnapshotsResponse>*>(writer_),
                                           cq_, cq_, this);
                break;
            default:
                //TODO handle error
                break;
        }

    }
        
    ~CatalogTask(){
        switch (type_) {
            case CatalogTaskType::GET_TABLE:
                delete static_cast<GetTableRequest*>(request_);
                delete static_cast<GetTableResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<GetTableResponse>*>(writer_);
                break;
            case CatalogTaskType::LIST_FILES:
                delete static_cast<ListFilesRequest*>(request_);
                delete static_cast<ListFilesResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<ListFilesResponse>*>(writer_);
                break;
            case CatalogTaskType::APPEND_FILES:
                delete static_cast<AppendFilesRequest*>(request_);
                delete static_cast<AppendFilesResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<AppendFilesResponse>*>(writer_);
                break;
            case CatalogTaskType::REWRITE_FILES:
                delete static_cast<RewriteFilesRequest*>(request_);
                delete static_cast<RewriteFilesResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<RewriteFilesResponse>*>(writer_);
                break;
            case CatalogTaskType::EXEC_QUERY:
                delete static_cast<ExecQueryRequest*>(request_);
                delete static_cast<ExecQueryResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<ExecQueryResponse>*>(writer_);
                break;
            case CatalogTaskType::START_TRANSACTION:
                delete static_cast<StartTransactionRequest*>(request_);
                delete static_cast<StartTransactionResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<StartTransactionResponse>*>(writer_);
                break;
            case CatalogTaskType::COMMIT:
                delete static_cast<CommitRequest*>(request_);
                delete static_cast<CommitResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<CommitResponse>*>(writer_);
                break;
            case CatalogTaskType::EXPIRE_SNAPSHOTS:
                delete static_cast<ExpireSnapshotsRequest*>(request_);
                delete static_cast<ExpireSnapshotsResponse*>(response_);
                delete static_cast<grpc::ServerAsyncResponseWriter<ExpireSnapshotsResponse>*>(writer_);
                break;
            default:
                //TODO handle error
                break;
        }

    }

};

}