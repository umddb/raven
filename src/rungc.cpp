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

// for running the catalog server
namespace ercat {

void runServer() {
    Config::init();
    std::string gc_graph_path = Config::getInstance().get("gc.graph.path");
    for (int i = 0; i < 20; i++) {
        const std::filesystem::path test_path = gc_graph_path + std::to_string(i);
        std::error_code ec;
        std::filesystem::remove_all(test_path, ec);
    }
    
    CatCache::init();
    GCManager::getInstance().start();
    std::string input;
    while (true) {
        std::cin >> input;
        
        if (input == "stop") {
            std::cout << "Stop command received. Exiting loop." << std::endl;
            break; // Exits the infinite loop
        } else {
            std::cout << "Unknown command. Type 'stop' to exit: " << std::endl;
        }
    }

    GCManager::getInstance().shutDown();
}

void runServer(const std::string& config_path) {
    Config::init(config_path);
    std::string gc_graph_path = Config::getInstance().get("gc.graph.path");
    for (int i = 0; i < 20; i++) {
        const std::filesystem::path test_path = gc_graph_path + std::to_string(i);
        std::error_code ec;
        std::filesystem::remove_all(test_path, ec);
    }
    
    CatCache::init();
    GCManager::getInstance().start();
    
    std::string input;
    while (true) {
        std::cin >> input;
        
        if (input == "stop") {
            std::cout << "Stop command received. Exiting loop." << std::endl;
            break; // Exits the infinite loop
        } else {
            std::cout << "Unknown command. Type 'stop' to exit: " << std::endl;
        }
    }

    GCManager::getInstance().shutDown();
}

}

int main(int argc, char **argv) {
    if (argc > 1) {
        ercat::runServer(std::string(argv[1]));
    }
    else {
        ercat::runServer();
    }

    return 0;
}