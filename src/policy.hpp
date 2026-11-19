#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SimData;

struct TreeNode {
    int feat = -1; // 0:urg, 1:w, 2:de, 3:dl, 4:qo
    bool is_min = true;
    double th = 0.0;
    int lo = 0;
    int hi = 0;
};

struct Tree {
    int n_nodes = 0;
    std::array<TreeNode, 10> nodes{}; // 1..9
};

struct Policy {
    int fleet;
    Tree tree;
};

extern const char* const FEAT_NAMES[5];
bool is_valid_tree(const Tree&);
Tree canonicalize_tree(const Tree&);
Policy parse_policy(const std::string&, const SimData&);
std::pair<int, Tree> load_csv_tree(const SimData&, const std::string&);
void save_csv_tree(const SimData&, const std::string&, int, const Tree&);
