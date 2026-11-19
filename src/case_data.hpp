#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct Hop {
    int node;
    double dt;
};

struct Arrival {
    double at;
    int o;
    int d;
    int u;
    double dl;
    int fid;
};

struct Event {
    double t;
    int type; // 0: arrival, 1: arrive_node, 2: load_done, 3: unload_done
    int idx;
    bool operator>(const Event& other) const {
        if (t != other.t) return t > other.t;
        if (type != other.type) return type > other.type;
        return idx > other.idx;
    }
};

struct FlowDef {
    int o_dept;
    int d_dept;
    double rate;
};

struct SimData {
    int n_nodes = 26;
    int n_flows = 9;
    double t_load = 0.6;
    double t_unload = 0.6;
    double t_inter = 0.36;
    double t_end = 3000.0;
    double warm = 300.0;
    int qcap = 300;
    int end_qmax = 30;
    int max_nodes = 9;
    int urgent_dest = 6;
    double alpha = 0.6;
    double c_agv = 15.0;
    double c_w = 0.6;
    double c_u = 100.0;
    double lam_hr = 170.4;
    std::string fixed_dir;
    std::vector<uint32_t> seeds;
    int min_fleet = 1;
    int max_fleet = 30;

    std::array<int, 7> pd_nodes{};
    std::array<int, 26> is_inter{};
    std::array<std::array<double, 26>, 26> sp_mat{};
    std::vector<FlowDef> flows;
    std::vector<Hop> comp_path[26][26][2];
    std::vector<std::vector<Arrival>> streams;
    std::vector<std::vector<Event>> base_events;
};

inline double round9(double x) {
    return std::llround(x * 1e9) / 1e9;
}

std::string read_text(const std::string& path);
SimData load_case(const std::string& case_json, const std::string& seed11_json, int extra_scenarios = 0);
int scenario_index(SimData& data, uint32_t seed);
