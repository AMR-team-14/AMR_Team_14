#pragma once
#include "json_parser.hpp"

// ============================================================================
// 2. Core Data Structures
// ============================================================================
struct EdgeDef {
    int u, v;
    double dist;
};
struct FlowDef {
    int o_st; // station index 0..n_stations-1
    int d_st;
    double rate;
};
struct Hop {
    int node;
    double dt;
};
struct Arrival {
    double at;
    int o_st;
    int d_st;
    int o_node;
    int d_node;
    int u;
    int fid;
};
struct Event {
    double t;
    int type; // 0: arrival, 1: arrive_node, 2: load_done, 3: unload_done
    int idx;
    bool operator>(const Event& o) const {
        if (t != o.t) return t > o.t;
        if (type != o.type) return type > o.type;
        return idx > o.idx;
    }
};
static const char* FEAT_NAMES[5] = {"urg", "w", "de", "dl", "qo"};
struct TreeNode {
    int feat = 0; // 0:urg, 1:w, 2:de, 3:dl, 4:qo
    bool is_min = true;
    double th = 0.0;
    int lo = 0;
    int hi = 0;
};
struct Tree {
    int n_nodes = 0;
    std::array<TreeNode, 10> nodes{}; // 1..9
};
struct Split {
    int feat;
    bool is_min;
    double th;
};
// Corridor = maximal atomic chain of degree-2 nodes (or paired parallel lanes)
struct Corridor {
    std::vector<int> edges;
    std::vector<int> rel_sign; // 0 if edge goes forward along corridor, 1 if reversed
    bool is_parallel_pair = false;
};
struct CaseData {
    std::string name;
    std::string title;
    int n_nodes = 0;
    int n_edges = 0;
    int n_stations = 0;
    int n_flows = 0;
    std::vector<std::string> node_names;
    std::vector<EdgeDef> edges;
    std::vector<int> pd;
    std::vector<int> node_to_st;
    std::vector<std::string> depts;
    std::vector<FlowDef> flows;
    std::string z0;
    std::string fixed_dir;
    std::vector<int> seeds;
    std::vector<int> parallel_partner; // -1 if none, else partner edge index
    std::vector<int> is_inter;         // 1 if undirected degree >= 3
    int n_junctions = 0;
    int n_parallel_pairs = 0;
    // Meta verification hints (optional)
    bool has_meta = false;
    double meta_sum_fd_z0 = -1.0;
    int meta_junctions = -1;
    // Physical & cost parameters
    int urgent_dest = 0;
    double alpha = 0.6;
    double vc = 50.0, ve = 60.0;
    double tl = 0.5, tu = 0.5;
    double at = 50.0, p_occ = 0.3;
    double scale = 1.2;
    double v_empty = 50.0, v_loaded = 50.0 / 1.2;
    double t_load = 0.6, t_unload = 0.6, t_inter = 0.36;
    double t_end = 3000.0, warm = 300.0;
    int qcap = 300, end_qmax = 30;
    int n_min = 1, n_max = 30, max_nodes = 9;
    double c_agv = 15.0, c_w = 0.6, c_u = 100.0;
    double total_flow = 0.0, lam_hr = 0.0;
    // Station flow balance (hourly unscaled flow rates)
    std::vector<double> st_in, st_out, st_net; // net = in - out (>0 supply of empty AGVs)
    std::vector<double> st_urg_orig;           // hourly urgent rate departing from station s
    // Degree-2 corridor decomposition for SCC-preserving dir moves
    std::vector<Corridor> corridors;
};
static inline double round9(double x) {
    return std::llround(x * 1e9) / 1e9;
}
class Mulberry32 {
    uint32_t state;
public:
    explicit Mulberry32(uint32_t seed) : state(seed) {}
    double rand() {
        state += 0x6D2B79F5u;
        uint32_t t = state;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return ((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0;
    }
};
