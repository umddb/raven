#include <cstdlib>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <thread>

#include "catalog/service/catalogservice.h"
#include "catalog/translator/translator.h"
#include "catalog/grpc/grpcserver.h"
#include "utils/catcache.h"
#include "gc/gcgraph.h"
#include "gc/gcmanager.h"

namespace ercat {

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

void execERQLDDL(std::string erql_path) {
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

void execERQL(std::string erql_path) {
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

}

int main(int argc, char **argv) {
    ercat::Config::init(std::string(argv[1]));
    
    ercat::CatCache::init();
    ercat::execERQLDDL(argv[2]);
    ercat::execERQL(argv[3]);

    // ercat::testGRPC();

    return 0;
}