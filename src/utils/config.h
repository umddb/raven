#pragma once

#include <string>
#include <unordered_map>

namespace ercat {

// Config object used to store config variables
class Config {
public:
    static void init();
    static void init(const std::string& config_file_path);
    static const Config& getInstance();
    static const std::string& getPGConnString();

    ~Config();
    std::string get(const std::string& key) const;
    bool contains(const std::string& key) const;

private:
    static Config& getInstanceImpl(const std::string* config_file_path = nullptr);
    static bool safeIsSpace(char c);
    
    static const std::unordered_map<std::string, std::string> default_config_;

    Config(const std::string* config_file_path);
    const std::string& getPGConnStringImpl() const;

    std::unordered_map<std::string, std::string> config_;
    std::string pg_conn_str_;
};
    
}