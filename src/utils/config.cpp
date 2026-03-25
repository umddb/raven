#include <algorithm>
#include <cctype>
#include "fmt/format.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "utils/config.h"

namespace ercat {

void Config::init() {
    getInstanceImpl();
}

void Config::init(const std::string& config_file_path) {
    getInstanceImpl(&config_file_path);
}

const Config& Config::getInstance() {
    return getInstanceImpl();
}

const std::string& Config::getPGConnString() {
    return getInstance().getPGConnStringImpl();
}

Config::~Config() { }

std::string Config::get(const std::string& key) const {
    if (config_.count(key) > 0) {
        return config_.at(key);
    }
    return "";
}

bool Config::contains(const std::string& key) const {
    return (config_.count(key) > 0);
}

Config& Config::getInstanceImpl(const std::string* config_file_path) {
    static Config instance(config_file_path);
    return instance;
}

bool Config::safeIsSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c));
}

const std::unordered_map<std::string, std::string> Config::default_config_ = {
    {"postgres.host", "localhost"},
    {"postgres.user", "koh"},
    {"postgres.password", "koh"},
    {"postgres.dbname", "koh"},
    {"postgres.connect_timeout", "30"},
    {"gc.num_workers", "4"},
    {"gc.io_uring.queuedepth", "128"},
    {"gc.buffer.numblocks", "1000"},
    {"io_max_combine_limit", "16"},
    {"gc.epoch_period", "5000"},
    {"gc.num_file_task_ingestors", "8"},
    {"gc.num_file_task_executors", "32"},
    {"gc.graph.path", "/tmp/gcgraph"},
    {"gc.graph.create", "true"},
    {"gc.graph.max_pages", "10000000"},
    {"gc.graph.num_rc_workers", "4"},
    {"gc.graph.num_ms_workers", "1"},
    {"gc.metrics_output", "/tmp/gcmetrics.json"},
    {"grpc.num_workers", "16"},
    {"grpc.server_address", "0.0.0.0:9876"},
    {"catalog.num_workers", "32"},
    {"aws.region", "us-east-2"}
};

Config::Config(const std::string* config_file_path) {
    // default config values
    for (const auto& config : default_config_) {
        config_.emplace(config);
    }
    
    // Populate the config from the config file
    if (config_file_path != nullptr) {
        std::ifstream config_file(*config_file_path);
        if (config_file) {
            std::string line;
            while (std::getline(config_file, line)) {
                std::istringstream line_stream(line);
                std::string key;
                if (std::getline(line_stream, key, '=')) {
                    std::string value;
                    if (std::getline(line_stream, value)) {
                        // strip white spaces
                        key.erase(std::remove_if(key.begin(), key.end(), safeIsSpace), key.end());
                        value.erase(std::remove_if(value.begin(), value.end(), safeIsSpace), value.end());
                        // override if the config is defined the file
                        config_.insert_or_assign(key, value);
                    }
                }
            }
            config_file.close();
        }
        else {
            std::cerr << "Warning: failed to open config file " << config_file_path << "\n";
        }    
    }
    
    fmt::format_to(
        std::back_inserter(pg_conn_str_),
        "host={} user={} password={} dbname={} connect_timeout={}",
        get("postgres.host"),
        get("postgres.user"),
        get("postgres.password"),
        get("postgres.dbname"),
        get("postgres.connect_timeout")
    );
}

const std::string& Config::getPGConnStringImpl() const {
    return pg_conn_str_;
}

}

