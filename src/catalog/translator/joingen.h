
#pragma once

#include "boost/unordered/unordered_flat_map.hpp"
#include "fmt/args.h"
#include "utils/catcache.h"

namespace ercat {

class JoinGen {
public:
    // vertex class for join graph
    struct Vertex {
        Vertex() : visited_(false) { }
        Vertex(int64_t ent_id, const std::string& ent_var) : ent_id_(ent_id), ent_var_(ent_var), visited_(false) { }
        // entity (type) id
        int64_t ent_id_;
        // entity variable
        std::string ent_var_;
        // whether vertex is visited
        bool visited_;
    };

    // edge class for join graph
    struct Edge {
        Edge() : visited_(false) { }
        Edge(size_t vid, int64_t rel_id, size_t reverse, bool is_reverse) : vid_(vid),
                rel_id_(rel_id), reverse_(reverse), is_reverse_(is_reverse), visited_(false) { }
        // vertex id of the destination vertex
        size_t vid_;
        // relation type
        int64_t rel_id_;
        // index of the reverse edge in the adjacency list
        size_t reverse_;
        // whether the edge (relation) is reverse
        bool is_reverse_;
        // whether edge is visited
        bool visited_;
    };

    JoinGen(const boost::unordered_flat_map<std::string, std::string>& entity_binding, 
            const std::vector<std::array<std::string, 3>>& rel_apps, std::vector<std::string>& errors);
    ~JoinGen();
    std::string next();
    bool valid();

private:
    static const std::string& srcString(bool reverse);
    static const std::string& destString(bool reverse);

    static const std::string src_str_;
    static const std::string dest_str_;
    
    // initializes join graph, resolving entity types, relation types etc.
    bool initGraph(const boost::unordered_flat_map<std::string, std::string>& entity_binding, 
        const std::vector<std::array<std::string, 3>>& rel_apps);
    // subroutine for checking if the graph is fully connected
    bool isConnected();
    // initializes the join generator
    void initGen(const boost::unordered_flat_map<std::string, std::string>& entity_binding);
    
    const CatCache& cat_cache_;
    std::vector<Vertex> vertices_;
    std::vector<std::vector<Edge>> edges_;
    // join order of vertices (entities)
    std::vector<size_t> join_order_;
    // inverse map of join order
    std::vector<size_t> join_order_inverse_;
    // SQL join template used for generating SQL from_list
    std::string join_template_;
    // Current entity id parameters
    std::vector<int64_t> ent_id_params_;
    // fmt arg store for populating join template with parameters
    fmt::dynamic_format_arg_store<fmt::format_context> format_arg_store_;
    // Current join output
    size_t cur_join_;
    // Total number of joins
    size_t num_joins_;
    //errors vector
    std::vector<std::string>& errors_;
};

}