#include <stack>

#include "boost/unordered/unordered_flat_map.hpp"
#include "catalog/translator/joingen.h"

namespace ercat {

JoinGen::JoinGen(const boost::unordered_flat_map<std::string, std::string>& entity_binding, 
        const std::vector<std::array<std::string, 3>>& rel_apps, std::vector<std::string>& errors) : 
        cat_cache_(CatCache::getInstance()), cur_join_(0), num_joins_(0), errors_(errors) {
    // if graph initialization (+ variable & type resolution) is successful and graph is connected, 
    // initialize the generator        
    // if (initGraph(entity_binding, rel_apps) && isConnected())
    if (initGraph(entity_binding, rel_apps)) {
        initGen(entity_binding);
    }
}

JoinGen::~JoinGen() { }

std::string JoinGen::next() {
    // compute the next ent id parameters
    size_t quotient = cur_join_;
    size_t remainder = 0;
    const auto& descendant_map = cat_cache_.descendantMap();
    for (int i = join_order_.size() - 1; i >= 0; i--) {
        auto descendants = descendant_map.at(vertices_[join_order_[i]].ent_id_);
        remainder = quotient % descendants->size();
        quotient /=  descendants->size();
        ent_id_params_[i] = descendants->at(remainder);
    }

    format_arg_store_.clear();
    for (auto& param : ent_id_params_){
        format_arg_store_.push_back(param);
    }
    
    cur_join_++;
    return fmt::vformat(join_template_, format_arg_store_);
}

bool JoinGen::valid() {
    return (cur_join_ < num_joins_) && (errors_.empty());
}

const std::string JoinGen::src_str_ = "src";

const std::string JoinGen::dest_str_ = "dest";

const std::string& JoinGen::srcString(bool reverse) {
    if (reverse) {
        return dest_str_;
    }
    return src_str_;
}

const std::string& JoinGen::destString(bool reverse) {
    if (reverse) {
        return src_str_;
    }
    return dest_str_;
}

bool JoinGen::initGraph(const boost::unordered_flat_map<std::string, std::string>& entity_binding, 
        const std::vector<std::array<std::string, 3>>& rel_apps) {
    const boost::unordered_flat_map<std::string, EntityEntry>& entity_map = cat_cache_.entityMap();
    const boost::unordered_flat_map<std::string, RelNameId>& rel_name_map = cat_cache_.relNameMap();
    const boost::unordered_flat_map<RelSig, RelId>& reverse_rel_map = cat_cache_.reverseRelMap();

    boost::unordered_flat_map<std::string, size_t> ent_var_map;
    vertices_.reserve(entity_binding.size());
    edges_.reserve(entity_binding.size());
    for (auto& rel_app : rel_apps) {
        // entity1
        const std::string& ent1_var = rel_app[1];
        int64_t ent1_id;
        // insert src vertex
        if (!ent_var_map.contains(ent1_var)) {
            // if ent1 is not valid entity type, insert error message and return early
            if (!entity_map.contains(entity_binding.at(ent1_var))) {
                errors_.emplace_back(entity_binding.at(ent1_var) + " is invalid entity type.");
                return false;   
            }
            // retrieve ent id from the catalog
            ent1_id = entity_map.at(entity_binding.at(ent1_var)).ent_id_;
            // insert entity1 to vertices and create adjancency list
            ent_var_map.emplace(ent1_var, vertices_.size());
            vertices_.emplace_back(ent1_id, ent1_var);
            edges_.emplace_back();
        }
        else {
            // retrieve ent id from the vertices
            ent1_id = vertices_[ent_var_map.at(ent1_var)].ent_id_;
        }

        // entity2
        const std::string& ent2_var = rel_app[2];
        int64_t ent2_id;
        // insert dest vertex
        if (!ent_var_map.contains(ent2_var)) {
            // if ent2 is of not of valid entity type, insert error message and return early
            if (!entity_map.contains(entity_binding.at(ent2_var))) {
                errors_.emplace_back(entity_binding.at(ent2_var) + " is invalid entity type.");
                return false;   
            }
            // retrieve ent id from the catalog
            ent2_id = entity_map.at(entity_binding.at(ent2_var)).ent_id_;
            // insert entity2 to vertices and create adjancency list
            ent_var_map.emplace(ent2_var, vertices_.size());
            vertices_.emplace_back(ent2_id, ent2_var);
            edges_.emplace_back();
        }
        else {
            // retrieve ent id from the vertices
            ent2_id = vertices_[ent_var_map.at(ent2_var)].ent_id_;
        }

        if(!rel_name_map.contains(rel_app[0])) {
            errors_.emplace_back(rel_app[0] + " is invalid relation type.");
            return false;
        }

        RelSig rel_sig(rel_name_map.at(rel_app[0]), ent1_id, ent2_id);
        
        if(!reverse_rel_map.contains(rel_sig)) {
            errors_.emplace_back(rel_sig.toString() + " is invalid relation signature");
            return false;
        }
        
        int64_t rel_id = reverse_rel_map.at(rel_sig);
        // finally insert the relation edge and its reverse edge
        size_t ent1_vid = ent_var_map.at(ent1_var);
        size_t ent2_vid = ent_var_map.at(ent2_var);
        std::vector<Edge>& ent1_edges = edges_[ent1_vid];
        std::vector<Edge>& ent2_edges = edges_[ent2_vid];
        ent1_edges.emplace_back(ent2_vid, rel_id, ent2_edges.size(), false);
        ent2_edges.emplace_back(ent1_vid, rel_id, ent1_edges.size() - 1, true);
    }

    // if rel_apps is empty, allow selecting from 1 entity or  multiple ctes
    if (rel_apps.empty()) {
        bool is_cte = true;
        for (auto& ent1_binding : entity_binding) {
            std::string ent1_var = ent1_binding.first;
            int64_t ent1_id;
            // special case for cte
            if (!entity_map.contains(entity_binding.at(ent1_var))) {
                ent1_id = -1;
            }
            else {
                ent1_id = entity_map.at(entity_binding.at(ent1_var)).ent_id_;
                is_cte = false;
            }
            ent_var_map.emplace(ent1_var, vertices_.size());
            vertices_.emplace_back(ent1_id, ent1_var);
            edges_.emplace_back();
        }

        if (!is_cte && entity_binding.size() != 1) {
            errors_.emplace_back("Join graph is not connected (Cartesian product not allowed).");
            return false;
        }

    }

    if (entity_binding.size() != ent_var_map.size()) {
        errors_.emplace_back("Join graph is not connected (Cartesian product not allowed).");
        return false;
    }

    return true;
}

bool JoinGen::isConnected() {
    bool is_connected = true;
    // conduct dfs
    std::stack<size_t> dfs_stack;
    vertices_[0].visited_ = true;
    dfs_stack.push(0);
    while (!dfs_stack.empty()) {
        size_t cur_vertex = dfs_stack.top();
        dfs_stack.pop();
        for (auto& neighbor : edges_[cur_vertex]) {
            if (!vertices_[neighbor.vid_].visited_) {
                vertices_[neighbor.vid_].visited_ = true;
                dfs_stack.push(neighbor.vid_);
            }
        }
    }
    // if there is a vertex that is not visited, the graph is not connected
    for (auto& vertex : vertices_) {
        if (!vertex.visited_) {
            is_connected = false;
        }
        vertex.visited_ = false;
    }

    if (!is_connected) {
        errors_.emplace_back("Join graph is not connected (Cartesian product not allowed).");
    }

    return is_connected;
}

void JoinGen::initGen(const boost::unordered_flat_map<std::string, std::string>& entity_binding) {
    std::stringstream join_template_ss;

    std::stack<size_t> dfs_stack;
    vertices_[0].visited_ = true;
    // special case for cte
    if (vertices_[0].ent_id_ < 0) {
        join_template_ss << entity_binding.at(vertices_[0].ent_var_) << " " << vertices_[0].ent_var_;
    }
    else {
        join_order_.reserve(vertices_.size());
        join_order_inverse_.resize(vertices_.size());
        dfs_stack.push(0);
        join_order_.push_back(0);
        join_order_inverse_[0] = 0;
        join_template_ss << "ent{0} " << vertices_[0].ent_var_;
    }
    
    while (!dfs_stack.empty()) {
        size_t cur_vertex = dfs_stack.top();
        dfs_stack.pop();
        for (auto& cur_edge : edges_[cur_vertex]) {
            // if current edge and its reverse is not visited
            if (!cur_edge.visited_) {
                // mark both the current edge and its reverse edge as visited
                cur_edge.visited_ = true;
                edges_[cur_edge.vid_][cur_edge.reverse_].visited_ = true;
                bool reverse = cur_edge.is_reverse_;
                std::string rel_name = vertices_[cur_vertex].ent_var_ + vertices_[cur_edge.vid_].ent_var_
                        + std::to_string(cur_edge.rel_id_);
                join_template_ss << " INNER JOIN rel" << cur_edge.rel_id_ << " " << rel_name << " ON ("
                        << vertices_[cur_vertex].ent_var_ << ".obj_id = " << rel_name << "." << srcString(reverse)<< "_obj_id AND "
                        << vertices_[cur_vertex].ent_var_ << ".vid = " << rel_name << "." << srcString(reverse)<< "_vid AND " 
                        << rel_name << ".delete_time IS NULL AND "
                        << rel_name << "." << srcString(reverse) << "_ent_id = {" 
                        << join_order_inverse_[cur_vertex] << "}";
                // if the dest vertex is visited        
                if (vertices_[cur_edge.vid_].visited_) {
                    join_template_ss << " AND " << vertices_[cur_edge.vid_].ent_var_ 
                        << ".obj_id = " << rel_name << "." << destString(reverse) << "_obj_id AND " 
                        << ".vid = " << rel_name << "." << destString(reverse) << "_vid AND " 
                        << rel_name << ".delete_time IS NULL AND "
                        << rel_name << "." << destString(reverse)
                        << "_ent_id = {" << join_order_inverse_[cur_edge.vid_] << "})";
                }
                // else add the new vertex and join on it
                else {
                    // mark the new vertex as visited and add to the join order
                    vertices_[cur_edge.vid_].visited_ = true;
                    dfs_stack.push(cur_edge.vid_);
                    join_order_.push_back(cur_edge.vid_);
                    join_order_inverse_[cur_edge.vid_] = join_order_.size() - 1;
                    join_template_ss << " AND " << rel_name << "." << destString(reverse) << "_ent_id = {" 
                            << (join_order_.size() - 1)
                            << "}) INNER JOIN ent{" << (join_order_.size() - 1) << "} "
                            << vertices_[cur_edge.vid_].ent_var_ << " ON ("
                            << vertices_[cur_edge.vid_].ent_var_ << ".obj_id = " << rel_name << "." 
                            << destString(reverse) << "_obj_id AND "
                            << rel_name << ".delete_time IS NULL AND "
                            << vertices_[cur_edge.vid_].ent_var_ << ".vid = " << rel_name << "." 
                            << destString(reverse) << "_vid)";
                }
            }   
        }

    }

    for (auto& vertex : vertices_) {
        if (vertex.ent_id_ < 0 && !vertex.visited_) {
            join_template_ss << ", " << entity_binding.at(vertex.ent_var_) << " " << vertex.ent_var_;
        }
    }

    // create the join template
    join_template_ = join_template_ss.str();
    // extra intialization
    num_joins_ = 1;
    ent_id_params_.resize(join_order_.size());
    format_arg_store_.reserve(join_order_.size(), 0);
    const auto& descendant_map = cat_cache_.descendantMap();
    for (auto& vertex : join_order_) {
        // multiply number of descendant entity types
        num_joins_ *= descendant_map.at(vertices_[vertex].ent_id_)->size();
    }
    
}

}
