#include <cstdlib>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <thread>

#include "fmt/format.h"

#include "catalog/service/catalogservice.h"
#include "catalog/translator/translator.h"
#include "catalog/grpc/grpcserver.h"
#include "utils/catcache.h"
#include "gc/gcgraph.h"
#include "gc/gcmanager.h"

namespace ercat {

void init() {
    Config::init();
    for (int i = 0; i < 20; i++) {
        const std::filesystem::path test_path = "/tmp/gcgraph" + std::to_string(i);
        std::error_code ec;
        std::filesystem::remove_all(test_path, ec);
    }
}

// void test1() {
//     GCGraph& gc_graph = GCGraph::getInstance();

//     std::srand(static_cast<unsigned int>(std::time(0)));
//     std::vector<ExtEdge> add_edges;
//     add_edges.emplace_back(ExtVertex(1, 0, 0), ExtVertex(1, 0, 1));
//     add_edges.emplace_back(ExtVertex(1, 0, 0), ExtVertex(1, 0, 2));
//     add_edges.emplace_back(ExtVertex(1, 0, 0), ExtVertex(1, 0, 3));
//     add_edges.emplace_back(ExtVertex(1, 0, 1), ExtVertex(1, 0, 4));
//     add_edges.emplace_back(ExtVertex(1, 0, 1), ExtVertex(1, 0, 6));
//     add_edges.emplace_back(ExtVertex(1, 0, 2), ExtVertex(1, 0, 4));
//     add_edges.emplace_back(ExtVertex(1, 0, 3), ExtVertex(1, 0, 2));
//     add_edges.emplace_back(ExtVertex(1, 0, 3), ExtVertex(1, 0, 5));
//     add_edges.emplace_back(ExtVertex(1, 0, 4), ExtVertex(1, 0, 9));
//     add_edges.emplace_back(ExtVertex(1, 0, 5), ExtVertex(1, 0, 7));
//     add_edges.emplace_back(ExtVertex(1, 0, 5), ExtVertex(1, 0, 8));
//     add_edges.emplace_back(ExtVertex(1, 0, 9), ExtVertex(1, 0, 7));
    

//     std::vector<ExtEdge> delete_edges;
//     delete_edges.emplace_back(ExtVertex(1, 0, 0), ExtVertex(1, 0, 1));
//     delete_edges.emplace_back(ExtVertex(1, 0, 3), ExtVertex(1, 0, 5));
//     delete_edges.emplace_back(ExtVertex(1, 0, 9), ExtVertex(1, 0, 7));
    
//     auto new_ext_vertices = gc_graph.updateGraphStore(add_edges, delete_edges);
//     // merge the vertices to remove duplicates
//     auto merged_new_ext_vertices = GCGraph::mergeVectors(new_ext_vertices, true, true);
//     // deallocate vectors
//     new_ext_vertices.clear();

//     auto new_vertices = gc_graph.updateVertexMaps(merged_new_ext_vertices);


//     auto result = gc_graph.updateGraphEngine(add_edges, delete_edges, new_vertices);

//     std::cout << "deleted vertices\n";

//     for (auto& vertex: result) {
//         std::cout << vertex << "\n";
//     }

//     std::vector<ExtVertex> vertices;
//     auto edges = gc_graph.execRC(result, vertices);

//     std::cout << "unreachable vertices\n";

//     for (auto& vertex: vertices) {
//         std::cout << vertex.obj_id_ << "\n";
//     }

//     std::cout << "unreachable edges\n";

//     for (auto& edge : edges) {
//         std::cout << edge.src_.obj_id_ << "," << edge.dest_.obj_id_  <<"\n";
//     }

//     gc_graph.postRCCommit(vertices, delete_edges, edges);

// }

// void test2() {
//     using Clock = std::chrono::high_resolution_clock;
//     using std::chrono::milliseconds;

//     GCGraph& gc_graph = GCGraph::getInstance();
//     std::srand(static_cast<unsigned int>(std::time(0)));
    
//     boost::unordered_flat_set<ExtEdge> added_edges;
//     std::vector<ExtEdge> add_edges;
//     added_edges.reserve(1000000);
//     add_edges.reserve(1000000);
//     for (int i = 0; i < 1000000; i++) {
//         ExtEdge ext_edge(ExtVertex(1, 0, rand() % 1000000), ExtVertex(1, 0, rand() % 1000000));
//         if (ext_edge.src_ != ext_edge.dest_ && !added_edges.contains(ext_edge)) {
//             add_edges.emplace_back(ext_edge);
//             added_edges.emplace(ext_edge);
//         }
//     }
    
//     boost::unordered_flat_set<int> deleted_edges;
//     std::vector<ExtEdge> delete_edges;
//     delete_edges.reserve(add_edges.size()/10);
//     for (size_t i = 0; i < add_edges.size()/10; i++) {
//         int idx = rand() % add_edges.size();
//         if (!deleted_edges.contains(idx)) {
//             delete_edges.emplace_back(add_edges[idx]);
//             deleted_edges.emplace(idx);
//         }
//     }

//     auto start_time = Clock::now();
    
//     auto new_ext_vertices = gc_graph.updateGraphStore(add_edges, delete_edges);

//     auto end_time = Clock::now();
    
//     // merge the vertices to remove duplicates
//     auto merged_new_ext_vertices = GCGraph::mergeVectors(new_ext_vertices, true, true);
//     // deallocate vectors
//     new_ext_vertices.clear();

//     auto new_vertices = gc_graph.updateVertexMaps(merged_new_ext_vertices);



//     auto result = gc_graph.updateGraphEngine(add_edges, delete_edges, new_vertices);
    


//     // std::cout << "deleted vertices\n";

//     // for (auto& vertex: result) {
//     //     std::cout << vertex << "\n";
//     // }
   

    
//     std::vector<ExtVertex> vertices;
//     auto edges = gc_graph.execRC(result, vertices);

    
    
//     // std::cout << "unreachable vertices\n";

//     // for (auto& vertex: vertices) {
//     //     std::cout << vertex.ent_id_ << "\n";
//     // }

//     // std::cout << "unreachable edges\n";

//     // for (auto& edge : edges) {
//     //     std::cout << edge.src_.ent_id_ << "," << edge.dest_.ent_id_  <<"\n";
//     // }
    
    
    
//     gc_graph.postRCCommit(vertices, delete_edges, edges);

    

//     GCManager& gc_manager = GCManager::getInstance();
//     gc_manager.start();


//     auto ms = std::chrono::duration_cast<milliseconds>(end_time - start_time);
//     std::cout << "\nTask completed in: " << ms.count() << " milliseconds." << std::endl;

// }

void test3() {
    using Clock = std::chrono::high_resolution_clock;
    using std::chrono::milliseconds;
    CatCache::init();
    GCManager& gc_manager = GCManager::getInstance();
    gc_manager.start();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

}

void test4() {
    Config::init();
    if (!CatCache::init()) {
        return;
    }

    //initCatCache(&cat_cache);

    std::vector<std::string> input_str;
    input_str.emplace_back("SELECT r.repo_name, a FROM Repo r, Asset a, Contains(r, a) WHERE r.repo_name = 'dolby'");
    input_str.emplace_back("SELECT r.repo_name, m.*, d FROM Contains(Repo r, Model m), Contains(r, DataSet d) WHERE r.repo_name = 'dolby'");
    input_str.emplace_back("SELECT a.name FROM Asset a");
    input_str.emplace_back("SELECT r FROM Repo r");
    input_str.emplace_back("SELECT r FROM temp r");
    input_str.emplace_back("ALTER ENTITY Repo ADD CONSTRAINT repo_root ROOT;");
    input_str.emplace_back("ALTER RELATIONSHIP Contains(Repo, Asset) "
        "SET Repo REFERENCES Asset;"
    );
    input_str.emplace_back("ALTER RELATIONSHIP Contains(Repo, Asset) "
        "SET Asset REFERENCES Repo;"
    );
    input_str.emplace_back("ALTER RELATIONSHIP Contains(Repo, Asset) "
        "SET RETENTION INTERVAL '7 days';"
    );
    input_str.emplace_back("CREATE ABSTRACT ENTITY Foo()");
    input_str.emplace_back("CREATE FILELIST ENTITY Foo2(foo2_name VARCHAR(255)) IMPLEMENTS Asset");
    input_str.emplace_back("CREATE RELATIONSHIP Depends(Asset, Asset)");
    input_str.emplace_back("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE");
    input_str.emplace_back("COMMIT");
    input_str.emplace_back("INSERT INTO ROOT ENTITY Repo (repo_name) VALUES ('blah')");
    input_str.emplace_back("WITH temp AS (SELECT r.vid FROM Repo r) SELECT t.vid FROM temp t");
    input_str.emplace_back("WITH temp AS (SELECT r.repo_name FROM Repo r) INSERT INTO ENTITY Repo (repo_name) SELECT t.repo_name FROM temp t");
    input_str.emplace_back("INSERT INTO ENTITY Repo (repo_name) VALUES ('foo') RETURNING vid");
    input_str.emplace_back("INSERT INTO RELATIONSHIP Contains(Repo, DataSet) VALUES ('Repo', 0, 1, 'DataSet', 2, 3)");
    input_str.emplace_back(
        "WITH temp AS (SELECT r.vid FROM Repo r) INSERT INTO RELATIONSHIP Contains(Repo, DataSet) "
        "SELECT 'Repo', 0, t.vid, 'DataSet', 2, 3 FROM temp t"
    );
    input_str.emplace_back(
        "DELETE FROM ROOT ENTITY Repo WHERE obj_id = 1"
    );
    input_str.emplace_back(
        "DELETE FROM RELATIONSHIP Contains(Repo, DataSet) WHERE src_obj_id = 1 AND dest_obj_id = 1"
    );
    input_str.emplace_back(
        "WITH temp AS (SELECT r.obj_id FROM Repo r WHERE r.repo_name = 'repo1') "
        "DELETE FROM RELATIONSHIP Contains(Repo, DataSet) WHERE src_obj_id = temp.obj_id"
    );
    input_str.emplace_back(
        "INSERT INTO FILELIST DataSet(1, 3) (file_path) VALUES ('foo5.txt'), ('foo6.txt')"
    );
    input_str.emplace_back(
        "PREPARE INSERT INTO FILELIST DataSet(1, 3) (file_path) VALUES ('foo5.txt'), ('foo6.txt')"
    );
    input_str.emplace_back(
        "DELETE FROM FILELIST DataSet(1, 4) VALUES ('foo5.txt'), ('foo6.txt')"
    );
    input_str.emplace_back(
        "PREPARE DELETE FROM FILELIST DataSet(1, 4) VALUES ('foo5.txt'), ('foo6.txt')"
    );
    input_str.emplace_back(
        "SELECT * FROM FILELIST SNAPSHOT DataSet(1, 4) WHERE tag = 'manifest'"
    );
    input_str.emplace_back(
        "SELECT * FROM FILELIST DELTA DataSet(1, 3, 6)"
    );
    input_str.emplace_back(
        "SELECT * FROM FILELIST DELTA DataSet(1, 3, 6) WHERE tag = 'manifest'"
    );

    input_str.emplace_back("SELECT r, a FROM r, a, Contains(r, a) WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT r, m FROM Repo r, Model m WHERE r.name = 'dolby'");
    input_str.emplace_back("SELECT r, m FROM Contains(r, m)");

    Translator translator;
    for (auto& input : input_str) {
        std::cout << "Input: " << input << "\n";
        std::string output = translator.translate(input);
        const auto & errors = translator.errors();
        if (errors.empty()) {
            std::cout << "Output: " <<  output << "\n";
        }
        else {
            std::cout << "Errors: ";
            for (auto& error : errors) {
                std::cout << error << "\n";
            }
        }
        std::cout << "\n";
    }
}




std::vector<std::string> loadFileWithDelimiter(const std::string& filename, char delimiter) {
    std::vector<std::string> tokens;
    std::ifstream file(filename);

    // Check if the file was opened successfully
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return tokens;
    }

    std::string token;
    // std::getline can take a third argument as a delimiter.
    // This reads the stream until it hits the delimiter, 
    // extracts the text, and discards the delimiter itself.
    while (std::getline(file, token, delimiter)) {
        // If the file ends with a newline after the last delimiter, 
        // or has empty segments, you might want to filter them:
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    file.close();
    return tokens;
}

void test5(std::string erql_path) {
    Config::init();
    if (!CatCache::init()) {
        return;
    }

    char sep = ';';

    std::vector<std::string> data = loadFileWithDelimiter(erql_path, sep);
    std::string pg_conn_str = Config::getPGConnString();
    PGconn* pg_conn = PGUtil::connectPG(pg_conn_str);

    Translator translator;
    for (auto& input : data) {
        std::cout << "Input: " << input << "\n\n";
        std::string output = translator.translate(input);
        const auto & errors = translator.errors();
        if (errors.empty()) {
            std::cout << "Output: " <<  output << "\n";
            PGUtil::execPGCommandOnce(&pg_conn, pg_conn_str, output);
            CatCache::refresh();
        }
        else {
            std::cout << "Errors: ";
            for (auto& error : errors) {
                std::cout << error << "\n";
            }
        }
        std::cout << "\n";
        
    }

}

void testGetTable() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    GetTableRequest request;
    request.set_workspace_name("workspace1");
    request.set_db_name("db1");
    request.set_table_name("table1");
    request.set_vid(0);

    GetTableResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->GetTable(&context, request, &response);
    if (status.ok()) {
       std::cout << response.success() << std::endl;
       const TableObject& table = response.table();
       
       std::cout << table.workspace_name() << std::endl;
       std::cout << table.db_name() << std::endl; 
       std::cout << table.table_name() << std::endl; 
       std::cout << table.table_obj_id() << std::endl;
       std::cout << table.snapshot_obj_id() << std::endl;
       std::cout << table.snapshot_vid() << std::endl;
       std::cout << table.create_time() << std::endl;
       std::cout << table.table_type() << std::endl;
    }

}

void testListFiles() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    ListFilesRequest request;
    request.set_snapshot_obj_id(1);
    request.set_snapshot_vid(1);

    ListFilesResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->ListFiles(&context, request, &response);
    if (status.ok()) {
        std::cout << response.success() << std::endl;
        for (auto& file: response.files()) {
            std::cout << file.path() << std::endl;
        }
    }

}

void testAppendFiles() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    AppendFilesRequest request;
    request.set_table_obj_id(1);
    request.set_snapshot_obj_id(1);
    request.set_snapshot_vid(1);
    FileObject* file = request.add_files();
    file->set_path("foo3.txt");

    AppendFilesResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->AppendFiles(&context, request, &response);
    if (status.ok()) {
        std::cout << response.success() << std::endl;
    }

}

void testRewriteFiles() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    RewriteFilesRequest request;
    request.set_table_obj_id(1);
    request.set_snapshot_obj_id(1);
    request.set_snapshot_vid(1);
    request.add_files_to_replace("foo1.txt");
    request.add_files_to_replace("foo2.txt");
    // FileObject* file = request.add_files_to_add();
    // file->set_path("foo4.txt");
    
    RewriteFilesResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->RewriteFiles(&context, request, &response);
    if (status.ok()) {
        std::cout << response.success() << std::endl;
    }

}

void testExecQuery() {
     CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    ExecQueryRequest request;
    request.set_query("SELECT file_path FROM FILELIST SNAPSHOT TableSnapshot(1,1)");

    ExecQueryResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->ExecQuery(&context, request, &response);
    if (status.ok()) {
        std::cout << response.success() << std::endl;
        std::cout << response.result_set() << std::endl;
    }
}

void testStartTxn() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    StartTransactionRequest request;
    request.set_workspace_name("workspace1");
    request.set_db_name("db1");
    request.add_table_names("table1");
    request.add_table_names("table2");

    StartTransactionResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->StartTransaction(&context, request, &response);
    if (status.ok()) {
        std::cout << response.success() << std::endl;
        for (auto& table : response.tables() ) {
            std::cout << table.table_name() << std::endl;
            std::cout << table.table_obj_id() << std::endl;
            std::cout << table.snapshot_obj_id() << std::endl;
            std::cout << table.snapshot_vid() << std::endl;
            
        }
    }
}

void testMultiTableTxn() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    StartTransactionRequest start_txn_request;
    start_txn_request.set_workspace_name("workspace1");
    start_txn_request.set_db_name("db1");
    start_txn_request.add_table_names("table1");
    start_txn_request.add_table_names("table2");

    StartTransactionResponse start_txn_response;
    grpc::ClientContext context;

    grpc::Status status = stub->StartTransaction(&context, start_txn_request, &start_txn_response);
    if (status.ok() && start_txn_response.success()) {
        std::cout << "started txn\n";
    }

    const TableObject& table1 = start_txn_response.tables(0);
    const TableObject& table2 = start_txn_response.tables(1);

    std::cout << table1.table_obj_id() << std::endl;
    std::cout << table2.table_obj_id() << std::endl;

    // append files to table 1
    AppendFilesRequest append_files_request;
    append_files_request.set_table_obj_id(table1.table_obj_id());
    append_files_request.set_snapshot_obj_id(table1.snapshot_obj_id());
    append_files_request.set_prepare(true);
    append_files_request.set_txn_id(start_txn_response.txn_id());
    FileObject* file1 = append_files_request.add_files();
    file1->set_path("foo1.txt");

    AppendFilesResponse append_files_response;
    grpc::ClientContext context2;
    
    status = stub->AppendFiles(&context2, append_files_request, &append_files_response);
    if (status.ok() && append_files_response.success()) {
        std::cout << "first prepare append succeeded" << std::endl;
    }

    // append files to table 1
    AppendFilesRequest append_files_request2;
    append_files_request2.set_table_obj_id(table1.table_obj_id());
    append_files_request2.set_snapshot_obj_id(table1.snapshot_obj_id());
    append_files_request2.set_prepare(false);
    append_files_request2.set_txn_id(start_txn_response.txn_id());
    FileObject* file2 = append_files_request2.add_files();
    file2->set_path("foo2.txt");

    AppendFilesResponse append_files_response2;
    grpc::ClientContext context3;
    
    status = stub->AppendFiles(&context3, append_files_request2, &append_files_response2);
    if (status.ok() && append_files_response2.success()) {
        std::cout << "second append succeeded" << std::endl;
    }

    RewriteFilesRequest rewrite_files_request;
    rewrite_files_request.set_table_obj_id(table2.table_obj_id());
    rewrite_files_request.set_snapshot_obj_id(table2.snapshot_obj_id());
    rewrite_files_request.set_prepare(false);
    rewrite_files_request.set_txn_id(start_txn_response.txn_id());
    FileObject* file3 = rewrite_files_request.add_files_to_add();
    file3->set_path("foo3.txt");

    RewriteFilesResponse rewrite_files_response;
    grpc::ClientContext context4;
    
    status = stub->RewriteFiles(&context4, rewrite_files_request, &rewrite_files_response);
    if (status.ok() && rewrite_files_response.success()) {
        std::cout << "rewrite succeeded" << std::endl;
    }

    CommitRequest commit_request;
    commit_request.set_txn_id(start_txn_response.txn_id());

    CommitResponse commit_response;
    grpc::ClientContext context5;

    status = stub->Commit(&context5, commit_request, &commit_response);
    if (status.ok() && commit_response.success()) {
        std::cout << "commit succeeded" << std::endl;
    }


}

void testGCEndToEnd() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();
    GCManager::getInstance().start();
    
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:9876", grpc::InsecureChannelCredentials());
    std::unique_ptr<Catalog::Stub> stub = Catalog::NewStub(channel);
    
    GetTableRequest request;
    request.set_workspace_name("workspace1");
    request.set_db_name("db1");
    request.set_table_name("table1");

    GetTableResponse response;
    grpc::ClientContext context;
    
    grpc::Status status = stub->GetTable(&context, request, &response);
    const TableObject& table1 = response.table();
       
    // append files to table 1
    AppendFilesRequest append_files_request;
    append_files_request.set_table_obj_id(table1.table_obj_id());
    append_files_request.set_snapshot_obj_id(table1.snapshot_obj_id());
    append_files_request.set_snapshot_vid(table1.snapshot_vid());
    append_files_request.set_prepare(false);
    FileObject* file1 = append_files_request.add_files();
    file1->set_path("/tmp/foo1.txt");
    FileObject* file2 = append_files_request.add_files();
    file2->set_path("/tmp/foo2.txt");

    AppendFilesResponse append_files_response;
    grpc::ClientContext context2;
    
    status = stub->AppendFiles(&context2, append_files_request, &append_files_response);
    if (status.ok() && append_files_response.success()) {
        std::cout << "first append succeeded" << std::endl;
    }

    RewriteFilesRequest rewrite_files_request;
    rewrite_files_request.set_table_obj_id(table1.table_obj_id());
    rewrite_files_request.set_snapshot_obj_id(table1.snapshot_obj_id());
    rewrite_files_request.set_snapshot_vid(table1.snapshot_vid() + 1);
    rewrite_files_request.set_prepare(false);
    
    rewrite_files_request.add_files_to_replace("/tmp/foo1.txt");
    rewrite_files_request.add_files_to_replace("/tmp/foo2.txt");

    RewriteFilesResponse rewrite_files_response;
    grpc::ClientContext context3;
    
    status = stub->RewriteFiles(&context3, rewrite_files_request, &rewrite_files_response);
    if (status.ok() && rewrite_files_response.success()) {
        std::cout << "rewrite succeeded" << std::endl;
    }

    ExpireSnapshotsRequest expire_snapshots_request;
    expire_snapshots_request.set_workspace_name("workspace1");
    expire_snapshots_request.set_db_name("db1");
    expire_snapshots_request.set_table_name("table1");
    expire_snapshots_request.set_vid(table1.snapshot_vid() + 1);

    ExpireSnapshotsResponse expire_snapshots_response;
    grpc::ClientContext context4;
    
    status = stub->ExpireSnapshots(&context4, expire_snapshots_request, &expire_snapshots_response);
    if (status.ok() && expire_snapshots_response.success()) {
        std::cout << "expire snapshots succeeded" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
}


void testDebug() {
    CatCache::init();
    CatalogService::getInstance().start();
    GRPCServer::getInstance().start();

    GRPCServer::getInstance().shutDown();
    CatalogService::getInstance().shutDown();
}

}

int main() {
    ercat::init();
    // ercat::CatCache::init();
    // ercat::test5("/home/keonwoo/research/safedatalifecycle/tests/setup.erql");
    // ercat::test5("/home/keonwoo/research/safedatalifecycle/tests/populate.erql");
    ercat::testGCEndToEnd();

    return 0;
}