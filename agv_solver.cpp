#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif
// ============================================================================
// 1. Minimal Recursive-Descent JSON DOM Parser (100% Schema-Safe, Zero Hardcode)
// ============================================================================
struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool bval = false;
    double nval = 0.0;
    std::string sval;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;
    bool has(const std::string& k) const { return type == Object && obj.count(k) > 0; }
    const JsonVal& at(const std::string& k) const {
        auto it = obj.find(k);
        if (it == obj.end()) {
            std::cerr << "JSON missing key: " << k << "\n";
            std::exit(1);
        }
        return it->second;
    }
    double num(const std::string& k, double def_v = 0.0) const {
        if (!has(k)) return def_v;
        return at(k).nval;
    }
    std::string str(const std::string& k, const std::string& def_v = "") const {
        if (!has(k)) return def_v;
        return at(k).sval;
    }
};
class JsonParser {
    const std::string& s;
    size_t i = 0;
    void skip_ws() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    std::string parse_string() {
        skip_ws();
        if (i >= s.size() || s[i] != '"') { std::cerr << "JSON expected '\"' at " << i << "\n"; std::exit(1); }
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                if (s[i] == 'n') out.push_back('\n');
                else if (s[i] == 't') out.push_back('\t');
                else if (s[i] == 'r') out.push_back('\r');
                else out.push_back(s[i]);
                ++i;
            } else {
                out.push_back(s[i++]);
            }
        }
        if (i < s.size() && s[i] == '"') ++i;
        return out;
    }
    JsonVal parse_val() {
        skip_ws();
        if (i >= s.size()) { std::cerr << "JSON unexpected EOF\n"; std::exit(1); }
        char c = s[i];
        if (c == '{') {
            JsonVal v; v.type = JsonVal::Object;
            ++i; skip_ws();
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            while (i < s.size()) {
                std::string k = parse_string();
                skip_ws();
                if (i < s.size() && s[i] == ':') ++i;
                v.obj[k] = parse_val();
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
            }
            return v;
        }
        if (c == '[') {
            JsonVal v; v.type = JsonVal::Array;
            ++i; skip_ws();
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            while (i < s.size()) {
                v.arr.push_back(parse_val());
                skip_ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
            }
            return v;
        }
        if (c == '"') {
            JsonVal v; v.type = JsonVal::String; v.sval = parse_string(); return v;
        }
        if (c == 't' && s.compare(i, 4, "true") == 0) {
            JsonVal v; v.type = JsonVal::Bool; v.bval = true; i += 4; return v;
        }
        if (c == 'f' && s.compare(i, 5, "false") == 0) {
            JsonVal v; v.type = JsonVal::Bool; v.bval = false; i += 5; return v;
        }
        if (c == 'n' && s.compare(i, 4, "null") == 0) {
            JsonVal v; v.type = JsonVal::Null; i += 4; return v;
        }
        // number
        size_t consumed = 0;
        double num = std::stod(s.substr(i), &consumed);
        i += consumed;
        JsonVal v; v.type = JsonVal::Number; v.nval = num; return v;
    }
public:
    explicit JsonParser(const std::string& src) : s(src) {}
    JsonVal parse() { return parse_val(); }
};
JsonVal load_json_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open JSON file: " << path << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf();
    std::string content = ss.str();
    JsonParser p(content);
    return p.parse();
}
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
// ============================================================================
// 3. Case Loading & Corridor Decomposition
// ============================================================================
CaseData load_case(const std::string& path, const std::string& cname) {
    JsonVal j = load_json_file(path);
    CaseData cd;
    cd.name = cname;
    cd.title = j.str("title", cname);
    const auto& jnodes = j.at("nodes").arr;
    cd.n_nodes = (int)jnodes.size();
    cd.node_names.resize(cd.n_nodes);
    for (int i = 0; i < cd.n_nodes; ++i) cd.node_names[i] = jnodes[i].arr[0].sval;
    const auto& jedges = j.at("edges").arr;
    cd.n_edges = (int)jedges.size();
    cd.edges.resize(cd.n_edges);
    for (int e = 0; e < cd.n_edges; ++e) {
        cd.edges[e] = EdgeDef{(int)jedges[e].arr[0].nval, (int)jedges[e].arr[1].nval, jedges[e].arr[2].nval};
    }
    const auto& jpd = j.at("pd").arr;
    cd.n_stations = (int)jpd.size();
    cd.pd.resize(cd.n_stations);
    cd.node_to_st.assign(cd.n_nodes, -1);
    for (int s = 0; s < cd.n_stations; ++s) {
        cd.pd[s] = (int)jpd[s].nval;
        cd.node_to_st[cd.pd[s]] = s;
    }
    const auto& jdepts = j.at("depts").arr;
    cd.depts.resize(cd.n_stations);
    for (int s = 0; s < cd.n_stations; ++s) cd.depts[s] = jdepts[s].sval;
    const auto& jflows = j.at("flows").arr;
    cd.n_flows = (int)jflows.size();
    cd.flows.resize(cd.n_flows);
    cd.total_flow = 0.0;
    for (int f = 0; f < cd.n_flows; ++f) {
        cd.flows[f] = FlowDef{(int)jflows[f].arr[0].nval, (int)jflows[f].arr[1].nval, jflows[f].arr[2].nval};
        cd.total_flow += cd.flows[f].rate;
    }
    const auto& jz0 = j.at("z0").arr;
    cd.z0.resize(cd.n_edges, '0');
    for (int e = 0; e < cd.n_edges; ++e) cd.z0[e] = ((int)jz0[e].nval != 0) ? '1' : '0';
    cd.fixed_dir = j.str("fixedDir", "");
    const auto& jseeds = j.at("seeds").arr;
    for (const auto& sv : jseeds) cd.seeds.push_back((int)sv.nval);
    cd.urgent_dest = (int)j.num("urgentDest", 0);
    cd.alpha = j.num("alpha", 0.6);
    cd.vc = j.num("vc", 50.0);
    cd.ve = j.num("ve", 60.0);
    cd.tl = j.num("tl", 0.5);
    cd.tu = j.num("tu", 0.5);
    cd.at = j.num("at", 50.0);
    cd.p_occ = j.num("p", 0.3);
    cd.t_end = j.num("T", 3000.0);
    cd.warm = j.num("warm", 300.0);
    cd.qcap = (int)j.num("qcap", 300);
    cd.end_qmax = (int)j.num("endQmax", 30);
    cd.n_min = (int)j.num("nMin", 1);
    cd.n_max = (int)j.num("nMax", 30);
    cd.max_nodes = (int)j.num("maxNodes", 9);
    cd.c_agv = j.num("cAGV", 15.0);
    cd.c_w = j.num("cW", 0.6);
    cd.c_u = j.num("cU", 100.0);
    cd.scale = 60.0 / cd.at;
    cd.v_empty = cd.ve / cd.scale;
    cd.v_loaded = cd.vc / cd.scale;
    cd.t_load = cd.tl * cd.scale;
    cd.t_unload = cd.tu * cd.scale;
    cd.t_inter = cd.p_occ * cd.scale;
    cd.lam_hr = cd.total_flow * cd.alpha;
    if (j.has("meta")) {
        cd.has_meta = true;
        const auto& jm = j.at("meta");
        if (jm.has("sumFD_z0")) cd.meta_sum_fd_z0 = jm.num("sumFD_z0");
        if (jm.has("junctions")) cd.meta_junctions = (int)jm.num("junctions");
    }
    // Parallel lane detection: edges e1=(u,v) and e2=(v,u)
    cd.parallel_partner.assign(cd.n_edges, -1);
    cd.n_parallel_pairs = 0;
    for (int e1 = 0; e1 < cd.n_edges; ++e1) {
        if (cd.parallel_partner[e1] != -1) continue;
        for (int e2 = e1 + 1; e2 < cd.n_edges; ++e2) {
            if (cd.parallel_partner[e2] != -1) continue;
            if (cd.edges[e1].u == cd.edges[e2].v && cd.edges[e1].v == cd.edges[e2].u) {
                cd.parallel_partner[e1] = e2;
                cd.parallel_partner[e2] = e1;
                ++cd.n_parallel_pairs;
                break;
            }
        }
    }
    // Junctions: undirected degree >= 3 (using unique undirected neighbors)
    std::vector<std::set<int>> unbr(cd.n_nodes);
    std::vector<std::vector<int>> inc_edges(cd.n_nodes);
    for (int e = 0; e < cd.n_edges; ++e) {
        unbr[cd.edges[e].u].insert(cd.edges[e].v);
        unbr[cd.edges[e].v].insert(cd.edges[e].u);
        inc_edges[cd.edges[e].u].push_back(e);
        inc_edges[cd.edges[e].v].push_back(e);
    }
    cd.is_inter.assign(cd.n_nodes, 0);
    cd.n_junctions = 0;
    for (int v = 0; v < cd.n_nodes; ++v) {
        if ((int)unbr[v].size() >= 3) {
            cd.is_inter[v] = 1;
            ++cd.n_junctions;
        }
    }
    // Station flow balance
    cd.st_in.assign(cd.n_stations, 0.0);
    cd.st_out.assign(cd.n_stations, 0.0);
    cd.st_net.assign(cd.n_stations, 0.0);
    cd.st_urg_orig.assign(cd.n_stations, 0.0);
    for (const auto& fl : cd.flows) {
        cd.st_out[fl.o_st] += fl.rate;
        cd.st_in[fl.d_st] += fl.rate;
        if (fl.d_st == cd.urgent_dest) cd.st_urg_orig[fl.o_st] += fl.rate;
    }
    for (int s = 0; s < cd.n_stations; ++s) cd.st_net[s] = cd.st_in[s] - cd.st_out[s];
    // Build degree-2 corridors (atomic SCC-preserving flip units!)
    std::vector<uint8_t> used_edge(cd.n_edges, 0);
    // 1) Parallel pairs first (keep dir[e1] == dir[e2])
    for (int e = 0; e < cd.n_edges; ++e) {
        if (cd.parallel_partner[e] > e) {
            Corridor cor;
            cor.edges = {e, cd.parallel_partner[e]};
            cor.rel_sign = {0, 0};
            cor.is_parallel_pair = true;
            cd.corridors.push_back(cor);
            used_edge[e] = 1;
            used_edge[cd.parallel_partner[e]] = 1;
        }
    }
    // 2) Chains starting from junctions (nodes with inc_edges.size() != 2)
    for (int start = 0; start < cd.n_nodes; ++start) {
        if ((int)inc_edges[start].size() == 2) continue;
        for (int e0 : inc_edges[start]) {
            if (used_edge[e0]) continue;
            Corridor cor;
            int cur_v = start;
            int cur_e = e0;
            while (true) {
                used_edge[cur_e] = 1;
                int nxt_v = (cd.edges[cur_e].u == cur_v) ? cd.edges[cur_e].v : cd.edges[cur_e].u;
                int rsign = (cd.edges[cur_e].u == cur_v) ? 0 : 1;
                cor.edges.push_back(cur_e);
                cor.rel_sign.push_back(rsign);
                cur_v = nxt_v;
                if ((int)inc_edges[cur_v].size() != 2) break;
                int e_next = (inc_edges[cur_v][0] == cur_e) ? inc_edges[cur_v][1] : inc_edges[cur_v][0];
                if (used_edge[e_next]) break;
                cur_e = e_next;
            }
            cd.corridors.push_back(cor);
        }
    }
    // 3) Any pure cycle of degree-2 nodes
    for (int e0 = 0; e0 < cd.n_edges; ++e0) {
        if (used_edge[e0]) continue;
        Corridor cor;
        int cur_v = cd.edges[e0].u;
        int cur_e = e0;
        while (!used_edge[cur_e]) {
            used_edge[cur_e] = 1;
            int nxt_v = (cd.edges[cur_e].u == cur_v) ? cd.edges[cur_e].v : cd.edges[cur_e].u;
            int rsign = (cd.edges[cur_e].u == cur_v) ? 0 : 1;
            cor.edges.push_back(cur_e);
            cor.rel_sign.push_back(rsign);
            cur_v = nxt_v;
            int e_next = (inc_edges[cur_v][0] == cur_e) ? inc_edges[cur_v][1] : inc_edges[cur_v][0];
            cur_e = e_next;
        }
        cd.corridors.push_back(cor);
    }
    return cd;
}
// ============================================================================
// 4. Graph Validation (Tarjan SCC == 1 + Parallel Lanes) & Shortest Paths
// ============================================================================
bool is_valid_dir(const CaseData& cd, const std::string& dir) {
    if ((int)dir.size() != cd.n_edges) return false;
    if (!cd.fixed_dir.empty() && dir != cd.fixed_dir) return false;
    for (int e = 0; e < cd.n_edges; ++e) {
        if (dir[e] != '0' && dir[e] != '1') return false;
        if (cd.parallel_partner[e] > e && dir[e] != dir[cd.parallel_partner[e]]) return false;
    }
    // Build directed adjacency list
    std::vector<std::vector<int>> adj(cd.n_nodes);
    for (int e = 0; e < cd.n_edges; ++e) {
        if (dir[e] == '0') adj[cd.edges[e].u].push_back(cd.edges[e].v);
        else adj[cd.edges[e].v].push_back(cd.edges[e].u);
    }
    // Tarjan SCC
    std::vector<int> dfn(cd.n_nodes, 0), low(cd.n_nodes, 0), st;
    std::vector<uint8_t> on_st(cd.n_nodes, 0);
    int timer = 0, scc_cnt = 0;
    auto dfs = [&](auto& self, int u) -> void {
        dfn[u] = low[u] = ++timer;
        st.push_back(u); on_st[u] = 1;
        for (int v : adj[u]) {
            if (!dfn[v]) {
                self(self, v);
                low[u] = std::min(low[u], low[v]);
            } else if (on_st[v]) {
                low[u] = std::min(low[u], dfn[v]);
            }
        }
        if (low[u] == dfn[u]) {
            ++scc_cnt;
            while (true) {
                int w = st.back(); st.pop_back(); on_st[w] = 0;
                if (w == u) break;
            }
        }
    };
    dfs(dfs, 0);
    if (scc_cnt != 1) return false;
    for (int v = 0; v < cd.n_nodes; ++v) if (!dfn[v]) return false;
    return true;
}
struct CompiledDir {
    std::string dir;
    std::vector<std::vector<double>> sp_st; // [S][S] shortest distance between P/D stations
    std::vector<std::vector<std::array<std::vector<Hop>, 2>>> comp_path; // [S][S][loaded]
    std::vector<std::vector<std::vector<int>>> raw_path; // [S][S] full vertex path for surrogate load
    std::vector<double> flow_dl; // [F] loaded distance of each flow
    double sum_fd = 0.0;
    double empty_lp = 0.0;
    double empty_stoch = 0.0;
    double urg_access = 0.0;
    double junc_sq = 0.0;
    double junc_max = 0.0;
};
// Exact Min-Cost Flow on S <= 16 station supply/demand bipartite graph (Successive Shortest Path)
double solve_empty_transport_lp(const CaseData& cd, const std::vector<std::vector<double>>& sp_st,
                                std::vector<std::tuple<int, int, double>>& out_flows) {
    int S = cd.n_stations;
    int SRC = 2 * S, SNK = 2 * S + 1, N = 2 * S + 2;
    struct MEdge { int to, rev; double cap, cost; };
    std::vector<std::vector<MEdge>> g(N);
    auto add_e = [&](int u, int v, double cap, double cost) {
        g[u].push_back({v, (int)g[v].size(), cap, cost});
        g[v].push_back({u, (int)g[u].size() - 1, 0.0, -cost});
    };
    double tot_sup = 0.0;
    for (int s = 0; s < S; ++s) {
        if (cd.st_net[s] > 1e-9) { add_e(SRC, s, cd.st_net[s], 0.0); tot_sup += cd.st_net[s]; }
        if (cd.st_net[s] < -1e-9) add_e(S + s, SNK, -cd.st_net[s], 0.0);
    }
    for (int i = 0; i < S; ++i) {
        if (cd.st_net[i] <= 1e-9) continue;
        for (int j = 0; j < S; ++j) {
            if (cd.st_net[j] >= -1e-9) continue;
            add_e(i, S + j, 1e9, sp_st[i][j]);
        }
    }
    double total_cost = 0.0, pushed = 0.0;
    std::vector<double> dist(N);
    std::vector<int> pv(N), pe(N);
    while (pushed + 1e-9 < tot_sup) {
        dist.assign(N, 1e18);
        std::vector<uint8_t> inq(N, 0);
        std::queue<int> q;
        dist[SRC] = 0.0; q.push(SRC); inq[SRC] = 1;
        while (!q.empty()) {
            int u = q.front(); q.pop(); inq[u] = 0;
            for (int ei = 0; ei < (int)g[u].size(); ++ei) {
                const auto& e = g[u][ei];
                if (e.cap > 1e-9 && dist[u] + e.cost < dist[e.to] - 1e-9) {
                    dist[e.to] = dist[u] + e.cost;
                    pv[e.to] = u; pe[e.to] = ei;
                    if (!inq[e.to]) { q.push(e.to); inq[e.to] = 1; }
                }
            }
        }
        if (dist[SNK] > 1e17) break;
        double f = tot_sup - pushed;
        for (int v = SNK; v != SRC; v = pv[v]) f = std::min(f, g[pv[v]][pe[v]].cap);
        for (int v = SNK; v != SRC; v = pv[v]) {
            auto& e = g[pv[v]][pe[v]];
            e.cap -= f;
            g[v][e.rev].cap += f;
        }
        pushed += f;
        total_cost += f * dist[SNK];
    }
    out_flows.clear();
    for (int i = 0; i < S; ++i) {
        for (const auto& e : g[i]) {
            if (e.to >= S && e.to < 2 * S) {
                double flow = 1e9 - e.cap;
                if (flow > 1e-6) out_flows.push_back({i, e.to - S, flow});
            }
        }
    }
    return total_cost;
}
CompiledDir compile_dir(const CaseData& cd, const std::string& dir, bool build_hops = true) {
    CompiledDir out;
    out.dir = dir;
    int V = cd.n_nodes, S = cd.n_stations;
    std::vector<std::vector<std::pair<int, double>>> adj(V);
    for (int e = 0; e < cd.n_edges; ++e) {
        int u = (dir[e] == '0') ? cd.edges[e].u : cd.edges[e].v;
        int v = (dir[e] == '0') ? cd.edges[e].v : cd.edges[e].u;
        adj[u].push_back({v, cd.edges[e].dist});
    }
    // Sort adjacency for deterministic shortest-path tie-breaking
    for (int u = 0; u < V; ++u) std::sort(adj[u].begin(), adj[u].end());
    out.sp_st.assign(S, std::vector<double>(S, 1e18));
    out.raw_path.assign(S, std::vector<std::vector<int>>(S));
    if (build_hops) out.comp_path.assign(S, std::vector<std::array<std::vector<Hop>, 2>>(S));
    std::vector<double> dist(V);
    std::vector<int> prev(V);
    std::vector<double> prev_w(V);
    for (int s1 = 0; s1 < S; ++s1) {
        int start = cd.pd[s1];
        dist.assign(V, 1e18);
        prev.assign(V, -1);
        prev_w.assign(V, 0.0);
        dist[start] = 0.0;
        using P = std::pair<double, int>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0.0, start});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u] + 1e-12) continue;
            for (auto [v, w] : adj[u]) {
                double nd = d + w;
                if (nd < dist[v] - 1e-12 || (std::abs(nd - dist[v]) <= 1e-12 && (prev[v] == -1 || u < prev[v]))) {
                    dist[v] = nd;
                    prev[v] = u;
                    prev_w[v] = w;
                    pq.push({nd, v});
                }
            }
        }
        for (int s2 = 0; s2 < S; ++s2) {
            int end = cd.pd[s2];
            out.sp_st[s1][s2] = dist[end];
            if (dist[end] > 1e17) continue;
            std::vector<int> path;
            std::vector<double> seg_w;
            for (int cur = end; cur != -1; cur = prev[cur]) {
                path.push_back(cur);
                if (prev[cur] != -1) seg_w.push_back(prev_w[cur]);
            }
            std::reverse(path.begin(), path.end());
            std::reverse(seg_w.begin(), seg_w.end());
            out.raw_path[s1][s2] = path;
            if (build_hops) {
                for (int ld = 0; ld < 2; ++ld) {
                    double spd = ld ? cd.v_loaded : cd.v_empty;
                    std::vector<Hop> hops;
                    double acc_t = 0.0;
                    for (size_t k = 0; k + 1 < path.size(); ++k) {
                        int b = path[k + 1];
                        acc_t += seg_w[k] / spd;
                        if (cd.is_inter[b] || k + 2 == path.size()) {
                            hops.push_back(Hop{b, acc_t});
                            acc_t = 0.0;
                        }
                    }
                    out.comp_path[s1][s2][ld] = std::move(hops);
                }
            }
        }
    }
    // Analytical Surrogate Metrics
    out.flow_dl.resize(cd.n_flows);
    out.sum_fd = 0.0;
    std::vector<double> jload(V, 0.0);
    for (int f = 0; f < cd.n_flows; ++f) {
        const auto& fl = cd.flows[f];
        double d = out.sp_st[fl.o_st][fl.d_st];
        out.flow_dl[f] = d;
        out.sum_fd += fl.rate * d;
        const auto& rp = out.raw_path[fl.o_st][fl.d_st];
        for (size_t k = 1; k < rp.size(); ++k) {
            if (cd.is_inter[rp[k]]) jload[rp[k]] += fl.rate * cd.alpha;
        }
    }
    // Exact Min-Cost Empty Repositioning LP
    std::vector<std::tuple<int, int, double>> emp_flows;
    out.empty_lp = solve_empty_transport_lp(cd, out.sp_st, emp_flows);
    for (const auto& [s1, s2, rate] : emp_flows) {
        const auto& rp = out.raw_path[s1][s2];
        for (size_t k = 1; k < rp.size(); ++k) {
            if (cd.is_inter[rp[k]]) jload[rp[k]] += rate * cd.alpha;
        }
    }
    // Stochastic empty travel expectation (captures Poisson mismatch across all station pairs)
    double tot_in = 0.0;
    for (int s = 0; s < S; ++s) tot_in += cd.st_in[s];
    out.empty_stoch = 0.0;
    for (int i = 0; i < S; ++i) {
        for (int j = 0; j < S; ++j) {
            out.empty_stoch += (cd.st_in[i] / std::max(1.0, tot_in)) * cd.st_out[j] * out.sp_st[i][j];
        }
    }
    // Urgent origin accessibility: distance from incoming stations to urgent origins
    out.urg_access = 0.0;
    for (int uo = 0; uo < S; ++uo) {
        if (cd.st_urg_orig[uo] <= 1e-9) continue;
        // Weight closer stations exponentially (soft-min access time to urgent origin)
        double wsum = 0.0, dsum = 0.0;
        for (int s = 0; s < S; ++s) {
            double wt = cd.st_in[s] * std::exp(-out.sp_st[s][uo] / 60.0);
            wsum += wt;
            dsum += wt * out.sp_st[s][uo];
        }
        double avg_near = (wsum > 1e-9) ? (dsum / wsum) : 200.0;
        // Also penalize long distance from urgentDest back to urgent origins / deficit stations
        out.urg_access += cd.st_urg_orig[uo] * (avg_near + 0.35 * out.sp_st[cd.urgent_dest][uo]);
    }
    out.junc_sq = 0.0;
    out.junc_max = 0.0;
    for (int v = 0; v < V; ++v) {
        if (!cd.is_inter[v]) continue;
        out.junc_sq += jload[v] * jload[v];
        out.junc_max = std::max(out.junc_max, jload[v]);
    }
    return out;
}
struct SurrWeights {
    double w_loaded = 1.0;
    double w_emp_lp = 0.90;
    double w_emp_st = 0.25;
    double w_urg = 0.35;
    double w_jsq = 0.08;
    double w_jmax = 15.0;
};
double eval_surrogate(const CaseData& cd, const CompiledDir& cdir, const SurrWeights& wt) {
    // Convert distances to AGV-min/hr units for scale-invariant surrogate
    double loaded_m = (cdir.sum_fd * cd.alpha) / cd.v_loaded;
    double emp_lp_m = (cdir.empty_lp * cd.alpha) / cd.v_empty;
    double emp_st_m = (cdir.empty_stoch * cd.alpha) / cd.v_empty;
    double urg_m    = (cdir.urg_access * cd.alpha) / cd.v_empty;
    double jsq_pen  = (cdir.junc_sq * cd.t_inter * cd.t_inter) / 60.0;
    double jmax_pen = (cdir.junc_max * cd.t_inter) / 60.0; // max junction utilization (0..1)
    return wt.w_loaded * loaded_m + wt.w_emp_lp * emp_lp_m + wt.w_emp_st * emp_st_m
         + wt.w_urg * urg_m + wt.w_jsq * jsq_pen + wt.w_jmax * jmax_pen * jmax_pen;
}
// ============================================================================
// 5. Arrival Stream Generator (CRN) & Fast Exact DES Simulator
// ============================================================================
struct StreamPack {
    std::vector<Arrival> arrivals;
    std::vector<Event> base_events;
};
StreamPack generate_stream(const CaseData& cd, uint32_t seed) {
    Mulberry32 rng(seed);
    double lam_min = cd.lam_hr / 60.0;
    std::vector<double> cum(cd.n_flows);
    double acc = 0.0;
    for (int f = 0; f < cd.n_flows; ++f) {
        acc += cd.flows[f].rate / cd.total_flow;
        cum[f] = acc;
    }
    StreamPack sp;
    sp.arrivals.reserve((size_t)(lam_min * cd.t_end * 1.15));
    double t = 0.0;
    while (true) {
        double u1 = std::max(1e-12, rng.rand());
        t += -std::log(u1) / lam_min;
        if (t > cd.t_end) break;
        double u2 = rng.rand();
        int f_idx = (int)(std::lower_bound(cum.begin(), cum.end(), u2) - cum.begin());
        if (f_idx >= cd.n_flows) f_idx = cd.n_flows - 1;
        int o_st = cd.flows[f_idx].o_st;
        int d_st = cd.flows[f_idx].d_st;
        int urg = (d_st == cd.urgent_dest) ? 1 : 0;
        sp.arrivals.push_back(Arrival{t, o_st, d_st, cd.pd[o_st], cd.pd[d_st], urg, f_idx});
    }
    sp.base_events.resize(sp.arrivals.size());
    for (size_t i = 0; i < sp.arrivals.size(); ++i) {
        sp.base_events[i] = Event{round9(sp.arrivals[i].at), 0, (int)i};
    }
    return sp;
}
std::vector<StreamPack> build_streams(const CaseData& cd, const std::vector<int>& seeds) {
    std::vector<StreamPack> out(seeds.size());
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)seeds.size(); ++i) {
        out[i] = generate_stream(cd, (uint32_t)seeds[i]);
    }
    return out;
}
// Strict Tree Validation (1 <= n_nodes <= maxNodes, root=1, in_deg==1 for 2..n, no path dup feat, urg th==0.5)
bool is_strict_tree(const Tree& t, int max_nodes = 9) {
    if (t.n_nodes < 1 || t.n_nodes > max_nodes) return false;
    bool visited[10] = {false};
    int in_deg[10] = {0};
    auto dfs = [&](auto& self, int curr, int path_mask, int feat_mask) -> bool {
        if (curr == 0) return true;
        if (curr < 1 || curr > t.n_nodes) return false;
        if (path_mask & (1 << curr)) return false;
        const TreeNode& tn = t.nodes[curr];
        if (tn.feat < 0 || tn.feat >= 5) return false;
        if (tn.feat == 0 && std::abs(tn.th - 0.5) > 1e-9) return false;
        if (feat_mask & (1 << tn.feat)) return false;
        visited[curr] = true;
        int np = path_mask | (1 << curr);
        int nf = feat_mask | (1 << tn.feat);
        return self(self, tn.lo, np, nf) && self(self, tn.hi, np, nf);
    };
    if (!dfs(dfs, 1, 0, 0)) return false;
    for (int i = 1; i <= t.n_nodes; ++i) {
        if (!visited[i]) return false;
        if (t.nodes[i].lo > 0) ++in_deg[t.nodes[i].lo];
        if (t.nodes[i].hi > 0) ++in_deg[t.nodes[i].hi];
    }
    if (in_deg[1] != 0) return false;
    for (int i = 2; i <= t.n_nodes; ++i) if (in_deg[i] != 1) return false;
    return true;
}
Tree canonicalize_tree(const Tree& t) {
    int order[10], cnt = 0, q[10], qh = 0, qt = 0;
    bool seen[10] = {false};
    q[qt++] = 1; seen[1] = true;
    while (qh < qt) {
        int curr = q[qh++];
        order[cnt++] = curr;
        for (int c : {t.nodes[curr].lo, t.nodes[curr].hi}) {
            if (c != 0 && !seen[c]) { seen[c] = true; q[qt++] = c; }
        }
    }
    int id_map[10] = {0};
    for (int i = 0; i < cnt; ++i) id_map[order[i]] = i + 1;
    Tree out;
    out.n_nodes = cnt;
    for (int i = 0; i < cnt; ++i) {
        int old = order[i];
        out.nodes[i + 1] = t.nodes[old];
        out.nodes[i + 1].lo = id_map[t.nodes[old].lo];
        out.nodes[i + 1].hi = id_map[t.nodes[old].hi];
    }
    return out;
}
std::string tree_key_str(const Tree& t) {
    std::ostringstream ss;
    ss << t.n_nodes;
    for (int i = 1; i <= t.n_nodes; ++i) {
        const auto& n = t.nodes[i];
        ss << "|" << n.feat << (n.is_min ? '<' : '>') << std::llround(n.th * 1000.0) << ":" << n.lo << "," << n.hi;
    }
    return ss.str();
}
static inline int eval_tree_bits(const Tree& tree, int u, double w, double de, double dl, int qo) {
    int bits = 0;
    int shift = 12;
    int nid = 1;
    while (nid != 0) {
        const TreeNode& tn = tree.nodes[nid];
        double val = (tn.feat == 0) ? (double)u
                   : (tn.feat == 1) ? w
                   : (tn.feat == 2) ? de
                   : (tn.feat == 3) ? dl : (double)qo;
        bool is_hi = val > tn.th;
        int bit = tn.is_min ? (is_hi ? 1 : 0) : (is_hi ? 0 : 1);
        bits |= (bit << shift);
        --shift;
        nid = is_hi ? tn.hi : tn.lo;
    }
    return bits;
}
struct SimMetrics {
    double cost = 999999.0;
    double w_bar = 999.0;
    double u_95 = 999.0;
    int max_q = 0;
    int end_q = 0;
    bool dq = true;
};
struct AgvState {
    int status = 0;
    int src_st = 0;
    int dst_st = 0;
    int hidx = 0;
    bool is_loaded = false;
    int rid = -1;
};
SimMetrics run_sim(const CaseData& cd, const CompiledDir& cdir, const StreamPack& sp,
                   int n_agv, const Tree& tree, int q_cutoff = -1, bool brute_force = false,
                   std::vector<double>* out_wait_samples = nullptr) {
    const auto& arrivals = sp.arrivals;
    const int n_arr = (int)arrivals.size();
    const int S = cd.n_stations;
    const int F = cd.n_flows;
    const int q_limit = (q_cutoff > 0) ? std::min(q_cutoff, cd.qcap) : cd.qcap;
    bool all_w_max = true;
    double w_min_th[10];
    int n_w_min = 0;
    if (!brute_force) {
        for (int nid = 1; nid <= tree.n_nodes; ++nid) {
            if (tree.nodes[nid].feat == 1 && tree.nodes[nid].is_min) {
                all_w_max = false;
                bool dup = false;
                for (int k = 0; k < n_w_min; ++k) if (std::abs(w_min_th[k] - tree.nodes[nid].th) < 1e-9) dup = true;
                if (!dup) w_min_th[n_w_min++] = tree.nodes[nid].th;
            }
        }
    }
    std::vector<Event> heap_buf = sp.base_events;
    std::make_heap(heap_buf.begin(), heap_buf.end(), std::greater<Event>());
    auto pq_push = [&](const Event& ev) {
        heap_buf.push_back(ev);
        std::push_heap(heap_buf.begin(), heap_buf.end(), std::greater<Event>());
    };
    auto pq_pop = [&]() -> Event {
        std::pop_heap(heap_buf.begin(), heap_buf.end(), std::greater<Event>());
        Event ev = heap_buf.back();
        heap_buf.pop_back();
        return ev;
    };
    std::vector<double> inter_free(cd.n_nodes, 0.0);
    int agv_st[64];
    AgvState agv_state[64];
    uint64_t idle_mask = (n_agv == 64) ? ~0ULL : ((1ULL << n_agv) - 1ULL);
    for (int i = 0; i < n_agv; ++i) agv_st[i] = i % S;
    std::vector<std::vector<int>> flow_q(F);
    std::vector<int> fq_head(F, 0);
    std::vector<int> qo_st(S, 0);
    int q_len = 0, max_q = 0;
    std::vector<double> pickup_t(n_arr, -1.0);
    auto start_leg = [&](double t, int aid, int target_st, bool is_loaded, int rid) {
        int cur_st = agv_st[aid];
        if (cur_st == target_st) {
            if (!is_loaded) {
                pickup_t[rid] = t;
                pq_push(Event{round9(t + cd.t_load), 2, aid});
                agv_state[aid].status = 2; agv_state[aid].rid = rid;
            } else {
                pq_push(Event{round9(t + cd.t_unload), 3, aid});
                agv_state[aid].status = 3; agv_state[aid].rid = rid;
            }
            return;
        }
        const auto& hops = cdir.comp_path[cur_st][target_st][is_loaded ? 1 : 0];
        agv_state[aid] = {1, cur_st, target_st, 0, is_loaded, rid};
        pq_push(Event{round9(t + hops[0].dt), 1, aid});
    };
    std::vector<int> cand_rids;
    cand_rids.reserve(310);
    auto try_dispatch = [&](double t) {
        while (idle_mask && q_len > 0) {
            int rep_st[64], rep_aid[64], n_rep = 0;
            if (brute_force) {
                uint64_t m = idle_mask;
                while (m) {
                    int aid = __builtin_ctzll(m); m &= m - 1;
                    rep_st[n_rep] = agv_st[aid]; rep_aid[n_rep] = aid; ++n_rep;
                }
            } else {
                uint64_t seen_st = 0, m = idle_mask;
                while (m) {
                    int aid = __builtin_ctzll(m); m &= m - 1;
                    int st = agv_st[aid];
                    uint64_t bit = 1ULL << st;
                    if (!(seen_st & bit)) {
                        seen_st |= bit; rep_st[n_rep] = st; rep_aid[n_rep] = aid; ++n_rep;
                    }
                }
            }
            cand_rids.clear();
            if (brute_force) {
                for (int fid = 0; fid < F; ++fid) {
                    const auto& fq = flow_q[fid];
                    for (int k = fq_head[fid]; k < (int)fq.size(); ++k) cand_rids.push_back(fq[k]);
                }
            } else if (all_w_max) {
                for (int fid = 0; fid < F; ++fid) {
                    if (fq_head[fid] < (int)flow_q[fid].size()) cand_rids.push_back(flow_q[fid][fq_head[fid]]);
                }
            } else {
                for (int fid = 0; fid < F; ++fid) {
                    int h = fq_head[fid], sz = (int)flow_q[fid].size();
                    if (h >= sz) continue;
                    const auto& fq = flow_q[fid];
                    int start_c = (int)cand_rids.size();
                    cand_rids.push_back(fq[h]);
                    for (int ti = 0; ti < n_w_min; ++ti) {
                        double min_at = t - w_min_th[ti];
                        int lo = h + 1, hi = sz;
                        while (lo < hi) {
                            int mid = (lo + hi) >> 1;
                            if (arrivals[fq[mid]].at >= min_at) hi = mid;
                            else lo = mid + 1;
                        }
                        if (lo < sz) {
                            int rid_c = fq[lo];
                            bool dup = false;
                            for (int x = start_c; x < (int)cand_rids.size(); ++x)
                                if (cand_rids[x] == rid_c) { dup = true; break; }
                            if (!dup) cand_rids.push_back(rid_c);
                        }
                    }
                }
            }
            int best_bits = 0x7FFFFFFF, best_rid = 0x7FFFFFFF, best_aid = 0x7FFFFFFF;
            double best_de = 1e18, best_at = 1e18;
            for (int r_i = 0; r_i < n_rep; ++r_i) {
                const auto& sp_row = cdir.sp_st[rep_st[r_i]];
                int aid = rep_aid[r_i];
                for (int rid : cand_rids) {
                    const Arrival& ar = arrivals[rid];
                    double de = sp_row[ar.o_st];
                    double dl = cdir.flow_dl[ar.fid];
                    int bits = eval_tree_bits(tree, ar.u, t - ar.at, de, dl, qo_st[ar.o_st]);
                    bool better = (bits != best_bits) ? (bits < best_bits)
                                : (de != best_de)     ? (de < best_de)
                                : (ar.at != best_at)  ? (ar.at < best_at)
                                : (rid != best_rid)   ? (rid < best_rid)
                                                      : (aid < best_aid);
                    if (better) {
                        best_bits = bits; best_de = de; best_at = ar.at; best_rid = rid; best_aid = aid;
                    }
                }
            }
            idle_mask &= ~(1ULL << best_aid);
            int fid = arrivals[best_rid].fid;
            if (best_rid == flow_q[fid][fq_head[fid]]) {
                ++fq_head[fid];
            } else {
                auto& fq = flow_q[fid];
                for (int k = fq_head[fid]; k < (int)fq.size(); ++k) {
                    if (fq[k] == best_rid) { fq.erase(fq.begin() + k); break; }
                }
            }
            --qo_st[arrivals[best_rid].o_st];
            --q_len;
            start_leg(t, best_aid, arrivals[best_rid].o_st, false, best_rid);
        }
    };
    while (!heap_buf.empty()) {
        double t = heap_buf.front().t;
        if (t > cd.t_end) break;
        bool need_dispatch = false;
        while (!heap_buf.empty() && std::abs(heap_buf.front().t - t) < 1e-9) {
            Event ev = pq_pop();
            if (ev.type == 0) {
                const Arrival& ar = arrivals[ev.idx];
                flow_q[ar.fid].push_back(ev.idx);
                ++qo_st[ar.o_st];
                ++q_len;
                if (q_len > max_q) max_q = q_len;
                if (max_q > q_limit) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len, true};
                need_dispatch = true;
            } else if (ev.type == 1) {
                int aid = ev.idx;
                AgvState& st = agv_state[aid];
                const auto& hops = cdir.comp_path[st.src_st][st.dst_st][st.is_loaded ? 1 : 0];
                int node = hops[st.hidx].node;
                double leave_t = t;
                if (cd.is_inter[node]) {
                    leave_t = std::max(t, inter_free[node]) + cd.t_inter;
                    inter_free[node] = leave_t;
                }
                if (st.hidx + 1 < (int)hops.size()) {
                    ++st.hidx;
                    pq_push(Event{round9(leave_t + hops[st.hidx].dt), 1, aid});
                } else {
                    agv_st[aid] = st.dst_st;
                    if (!st.is_loaded) {
                        pickup_t[st.rid] = leave_t;
                        pq_push(Event{round9(leave_t + cd.t_load), 2, aid});
                        st.status = 2;
                    } else {
                        pq_push(Event{round9(leave_t + cd.t_unload), 3, aid});
                        st.status = 3;
                    }
                }
            } else if (ev.type == 2) {
                int aid = ev.idx;
                int rid = agv_state[aid].rid;
                start_leg(t, aid, arrivals[rid].d_st, true, rid);
            } else {
                int aid = ev.idx;
                agv_state[aid].status = 0;
                idle_mask |= (1ULL << aid);
                need_dispatch = true;
            }
        }
        if (need_dispatch) try_dispatch(t);
    }
    if (q_len > cd.end_qmax) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len, true};
    double sum_w = 0.0;
    int cnt_all = 0;
    std::vector<double> waits_urg;
    waits_urg.reserve(2000);
    for (int rid = 0; rid < n_arr; ++rid) {
        const Arrival& ar = arrivals[rid];
        if (ar.at >= cd.warm && pickup_t[rid] >= 0.0 && pickup_t[rid] <= cd.t_end + 1e-9) {
            double wt = pickup_t[rid] - ar.at;
            sum_w += wt;
            ++cnt_all;
            if (ar.u == 1) waits_urg.push_back(wt);
            if (out_wait_samples && (rid % 7 == 0)) out_wait_samples->push_back(wt);
        }
    }
    if (cnt_all == 0 || waits_urg.empty()) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len, true};
    double w_bar = sum_w / cnt_all;
    std::sort(waits_urg.begin(), waits_urg.end());
    double pos = 0.95 * (waits_urg.size() - 1);
    size_t lo = (size_t)std::floor(pos), hi = (size_t)std::ceil(pos);
    double u95 = waits_urg[lo] + (pos - lo) * (waits_urg[hi] - waits_urg[lo]);
    double cost = cd.c_agv * n_agv + cd.c_w * cd.lam_hr * w_bar + cd.c_u * u95;
    return SimMetrics{cost, w_bar, u95, max_q, q_len, false};
}
struct EvalSummary {
    double mean_cost = 999999.0;
    double std_cost = 0.0;
    double p95_cost = 999999.0;
    double robust_score = 999999.0; // Mean + 0.2 * (P95 - Mean)
    double mean_w = 999.0;
    double mean_u95 = 999.0;
    int dq_cnt = 0;
    int max_q = 0;
    int max_end_q = 0;
};
EvalSummary eval_streams(const CaseData& cd, const CompiledDir& cdir,
                         const std::vector<StreamPack>& streams, int n_agv,
                         const Tree& tree, int count = -1, int q_cut = -1) {
    int N = (count > 0 && count < (int)streams.size()) ? count : (int)streams.size();
    std::vector<SimMetrics> ms(N);
    #pragma omp parallel for schedule(dynamic, 2)
    for (int i = 0; i < N; ++i) {
        ms[i] = run_sim(cd, cdir, streams[i], n_agv, tree, q_cut, false);
    }
    EvalSummary es;
    std::vector<double> costs;
    costs.reserve(N);
    double sc = 0.0, sc2 = 0.0, sw = 0.0, su = 0.0;
    for (const auto& m : ms) {
        es.max_q = std::max(es.max_q, m.max_q);
        es.max_end_q = std::max(es.max_end_q, m.end_q);
        if (m.dq) { ++es.dq_cnt; continue; }
        costs.push_back(m.cost);
        sc += m.cost; sc2 += m.cost * m.cost; sw += m.w_bar; su += m.u_95;
    }
    if (es.dq_cnt > 0 || costs.empty()) return es;
    int ok = (int)costs.size();
    es.mean_cost = sc / ok;
    es.mean_w = sw / ok;
    es.mean_u95 = su / ok;
    es.std_cost = (ok > 1) ? std::sqrt(std::max(0.0, (sc2 - ok * es.mean_cost * es.mean_cost) / (ok - 1))) : 0.0;
    std::sort(costs.begin(), costs.end());
    double pos = 0.95 * (ok - 1);
    size_t lo = (size_t)std::floor(pos), hi = (size_t)std::ceil(pos);
    es.p95_cost = costs[lo] + (pos - lo) * (costs[hi] - costs[lo]);
    es.robust_score = es.mean_cost + 0.2 * (es.p95_cost - es.mean_cost);
    return es;
}
double eval_crn_fast(const CaseData& cd, const CompiledDir& cdir,
                     const std::vector<StreamPack>& streams, int n_agv,
                     const Tree& tree, int cnt, double abort_thresh, int q_cut = 90) {
    double sum = 0.0;
    for (int k = 0; k < cnt; ++k) {
        auto m = run_sim(cd, cdir, streams[k], n_agv, tree, q_cut, false);
        if (m.dq) return 999999.0;
        sum += m.cost;
        if (k == 5 && cnt >= 12 && (sum / 6.0) > abort_thresh) return 999999.0;
    }
    return sum / cnt;
}
// ============================================================================
// 6. Stage A: Analytical Surrogate Dir Optimizer (SCC==1 + Parallel Lanes)
// ============================================================================
std::string flip_corridor(const CaseData& cd, std::string dir, int cid) {
    for (int e : cd.corridors[cid].edges) dir[e] = (dir[e] == '0') ? '1' : '0';
    return dir;
}
// Find a random simple directed cycle in current dir and reverse all corridors on it
bool try_cycle_reversal(const CaseData& cd, const std::string& dir, std::string& out_dir, std::mt19937& rng) {
    int V = cd.n_nodes;
    std::vector<std::vector<std::pair<int, int>>> adj(V); // (to, edge_idx)
    for (int e = 0; e < cd.n_edges; ++e) {
        if (cd.parallel_partner[e] != -1) continue; // skip parallel bridge from simple cycle reversal
        int u = (dir[e] == '0') ? cd.edges[e].u : cd.edges[e].v;
        int v = (dir[e] == '0') ? cd.edges[e].v : cd.edges[e].u;
        adj[u].push_back({v, e});
    }
    int start = rng() % V;
    std::vector<int> vis(V, 0), par_v(V, -1), par_e(V, -1);
    std::vector<int> path_v = {start};
    vis[start] = 1;
    int cur = start;
    for (int step = 0; step < V + 5; ++step) {
        if (adj[cur].empty()) return false;
        auto [nxt, e] = adj[cur][rng() % adj[cur].size()];
        if (vis[nxt]) {
            // Reconstruct cycle from nxt..cur + edge e
            out_dir = dir;
            out_dir[e] = (out_dir[e] == '0') ? '1' : '0';
            int x = cur;
            int len = 1;
            while (x != nxt && x != -1) {
                int pe = par_e[x];
                if (pe < 0) return false;
                out_dir[pe] = (out_dir[pe] == '0') ? '1' : '0';
                x = par_v[x];
                ++len;
            }
            return (x == nxt && len >= 3 && is_valid_dir(cd, out_dir));
        }
        vis[nxt] = 1; par_v[nxt] = cur; par_e[nxt] = e;
        cur = nxt;
    }
    return false;
}
int hamming_dir(const std::string& a, const std::string& b) {
    int d = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++d;
    return d;
}
std::vector<CompiledDir> optimize_dirs_stage_a(const CaseData& cd, int top_k = 15) {
    if (!cd.fixed_dir.empty()) {
        std::vector<CompiledDir> one;
        one.push_back(compile_dir(cd, cd.fixed_dir, true));
        return one;
    }
    // Collect non-parallel corridor indices (parallel pair flipping 00<->11 is graph-isomorphic)
    std::vector<int> free_cids;
    for (int c = 0; c < (int)cd.corridors.size(); ++c) {
        if (!cd.corridors[c].is_parallel_pair) free_cids.push_back(c);
    }
    int C = (int)free_cids.size();
    // Multiple surrogate weight profiles to capture diverse trade-offs (loaded vs empty vs urgent vs junction)
    std::vector<SurrWeights> profiles = {
        {1.00, 0.90, 0.25, 0.35, 0.08, 15.0}, // Balanced primary
        {1.00, 1.10, 0.35, 0.20, 0.05, 10.0}, // Pure flow + empty LP focus
        {0.85, 0.85, 0.25, 0.80, 0.08, 15.0}, // Urgent access focus
        {0.90, 0.80, 0.20, 0.30, 0.25, 45.0}, // Heavy junction dispersion (c4 / c5)
    };
    const SurrWeights& base_wt = profiles[0];
    std::map<std::string, double> pool_best_score;
    pool_best_score[cd.z0] = eval_surrogate(cd, compile_dir(cd, cd.z0, false), base_wt);
    if (C <= 16) {
        // EXACT EXHAUSTIVE ENUMERATION OF ALL 2^C CORRIDOR ORIENTATIONS! (c3: 2^9=512, c5: 2^13~14!)
        uint32_t total = 1u << C;
        std::vector<std::string> valid_dirs;
        for (uint32_t mask = 0; mask < total; ++mask) {
            std::string d = cd.z0;
            for (int b = 0; b < C; ++b) {
                if ((mask >> b) & 1u) {
                    for (int e : cd.corridors[free_cids[b]].edges) d[e] = (d[e] == '0') ? '1' : '0';
                }
            }
            if (is_valid_dir(cd, d)) valid_dirs.push_back(std::move(d));
        }
        std::cout << "  [Stage A] Exact corridor enumeration (" << C << " corridors -> "
                  << valid_dirs.size() << " valid SCC orientations)\n" << std::flush;
        std::vector<std::array<double, 4>> scores(valid_dirs.size());
        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < (int)valid_dirs.size(); ++i) {
            CompiledDir cdir = compile_dir(cd, valid_dirs[i], false);
            for (int p = 0; p < 4; ++p) scores[i][p] = eval_surrogate(cd, cdir, profiles[p]);
        }
        for (int p = 0; p < 4; ++p) {
            std::vector<int> ord(valid_dirs.size());
            std::iota(ord.begin(), ord.end(), 0);
            std::sort(ord.begin(), ord.end(), [&](int a, int b) { return scores[a][p] < scores[b][p]; });
            for (int r = 0; r < std::min((int)ord.size(), 40); ++r) {
                int idx = ord[r];
                const std::string& d = valid_dirs[idx];
                double bs = scores[idx][0];
                if (!pool_best_score.count(d) || bs < pool_best_score[d]) pool_best_score[d] = bs;
            }
        }
    } else {
        // Multi-Start Corridor SA + 2-Corridor Swap + Directed Cycle Reversal + Tabu (c2, c4!)
        int n_starts = (C <= 26) ? 48 : 64;
        int iters_per_start = (C <= 26) ? 3000 : 4000;
        std::cout << "  [Stage A] Multi-start Corridor SA + Cycle-Reversal + Tabu (" << C << " corridors, "
                  << n_starts << " restarts x " << iters_per_start << " iters)\n" << std::flush;
        std::vector<std::vector<std::pair<double, std::string>>> thread_pools(n_starts);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int st = 0; st < n_starts; ++st) {
            std::mt19937 rng(70001 + st * 1337 + cd.n_edges * 31);
            const SurrWeights& wt = profiles[st % profiles.size()];
            // Start from z0 + random valid corridor/cycle perturbations for true multi-start diversity
            std::string cur = cd.z0;
            if (st > 0) {
                int n_kick = 6 + (rng() % 18);
                for (int k = 0; k < n_kick; ++k) {
                    std::string cand;
                    if (rng() % 3 == 0) {
                        if (try_cycle_reversal(cd, cur, cand, rng)) cur = cand;
                    } else if (rng() % 2 == 0) {
                        int c1 = free_cids[rng() % C];
                        cand = flip_corridor(cd, cur, c1);
                        if (is_valid_dir(cd, cand)) cur = cand;
                    } else {
                        int c1 = free_cids[rng() % C], c2 = free_cids[rng() % C];
                        if (c1 != c2) {
                            cand = flip_corridor(cd, flip_corridor(cd, cur, c1), c2);
                            if (is_valid_dir(cd, cand)) cur = cand;
                        }
                    }
                }
            }
            CompiledDir cur_c = compile_dir(cd, cur, false);
            double cur_sc = eval_surrogate(cd, cur_c, wt);
            double best_sc = cur_sc;
            std::string best_d = cur;
            std::set<std::string> tabu;
            tabu.insert(cur);
            std::uniform_real_distribution<double> u01(0.0, 1.0);
            double T0 = std::max(5.0, cur_sc * 0.08), T1 = 0.05;
            for (int it = 0; it < iters_per_start; ++it) {
                double frac = (double)it / iters_per_start;
                double temp = T0 * std::pow(T1 / T0, frac);
                std::string cand;
                bool ok = false;
                int mv = rng() % 10;
                if (mv < 4) {
                    int c1 = free_cids[rng() % C];
                    cand = flip_corridor(cd, cur, c1);
                    ok = is_valid_dir(cd, cand);
                } else if (mv < 7) {
                    int c1 = free_cids[rng() % C], c2 = free_cids[rng() % C];
                    if (c1 != c2) {
                        cand = flip_corridor(cd, flip_corridor(cd, cur, c1), c2);
                        ok = is_valid_dir(cd, cand);
                    }
                } else {
                    ok = try_cycle_reversal(cd, cur, cand, rng);
                }
                if (!ok) continue;
                if (tabu.count(cand) && u01(rng) > 0.15) continue;
                CompiledDir cc = compile_dir(cd, cand, false);
                double sc = eval_surrogate(cd, cc, wt);
                double delta = sc - cur_sc;
                if (delta < 0.0 || u01(rng) < std::exp(-delta / temp)) {
                    cur = cand; cur_sc = sc;
                    if (tabu.size() < 2000) tabu.insert(cur);
                    if (sc < best_sc) {
                        best_sc = sc; best_d = cur;
                    }
                }
            }
            // Greedy 1-corridor + 2-corridor + cycle polish on best_d
            cur = best_d; cur_sc = best_sc;
            for (int pass = 0; pass < 4; ++pass) {
                bool imp = false;
                for (int c1 : free_cids) {
                    std::string cand = flip_corridor(cd, cur, c1);
                    if (!is_valid_dir(cd, cand)) continue;
                    double sc = eval_surrogate(cd, compile_dir(cd, cand, false), wt);
                    if (sc < cur_sc - 1e-6) { cur = cand; cur_sc = sc; imp = true; }
                }
                if (!imp) break;
            }
            double base_sc = eval_surrogate(cd, compile_dir(cd, cur, false), base_wt);
            thread_pools[st].push_back({base_sc, cur});
        }
        for (const auto& tp : thread_pools) {
            for (const auto& [sc, d] : tp) {
                if (!pool_best_score.count(d) || sc < pool_best_score[d]) pool_best_score[d] = sc;
            }
        }
    }
    // Sort all discovered valid dirs by primary surrogate and select K diverse winners!
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& [d, sc] : pool_best_score) ranked.push_back({sc, d});
    std::sort(ranked.begin(), ranked.end());
    int min_hamming = std::max(2, cd.n_edges / 12);
    std::vector<CompiledDir> selected;
    for (int relax = 0; relax < 3 && (int)selected.size() < top_k; ++relax) {
        int req_h = (relax == 0) ? min_hamming : (relax == 1 ? std::max(1, min_hamming / 2) : 1);
        for (const auto& [sc, d] : ranked) {
            if ((int)selected.size() >= top_k) break;
            bool too_close = false;
            for (const auto& ex : selected) {
                if (hamming_dir(ex.dir, d) < req_h) { too_close = true; break; }
            }
            if (!too_close) selected.push_back(compile_dir(cd, d, true));
        }
    }
    // Always ensure z0 is included if room or for reference comparison
    bool has_z0 = false;
    for (const auto& ex : selected) if (ex.dir == cd.z0) has_z0 = true;
    if (!has_z0 && (int)selected.size() < top_k) selected.push_back(compile_dir(cd, cd.z0, true));
    std::cout << "  [Stage A] Selected " << selected.size() << " diverse valid dirs. Best surr="
              << std::fixed << std::setprecision(2) << eval_surrogate(cd, selected[0], base_wt)
              << " (sumFD=" << selected[0].sum_fd << ", empLP=" << selected[0].empty_lp
              << ", jMax=" << selected[0].junc_max << ") vs z0 sumFD="
              << compile_dir(cd, cd.z0, false).sum_fd << "\n" << std::flush;
    return selected;
}
// ============================================================================
// 7. Stage B: Data-Driven Dynamic Palette + Fleet Search + Beam/LNS Tree Opt
// ============================================================================
struct Palette {
    std::vector<Split> core; // ~26-34 compact splits for beam search & LNS
    std::vector<Split> full; // ~45-60 full splits for point edits & local polish
    std::vector<double> de_vals;
    std::vector<double> dl_vals;
};
Palette build_dynamic_palette(const CaseData& cd, const CompiledDir& cdir,
                              const std::vector<StreamPack>& train_streams, int ref_n) {
    Palette pal;
    // Exact distinct de values from sp_st
    std::set<long long> de_set, dl_set;
    for (int i = 0; i < cd.n_stations; ++i)
        for (int j = 0; j < cd.n_stations; ++j)
            de_set.insert(std::llround(cdir.sp_st[i][j] * 100.0));
    for (int f = 0; f < cd.n_flows; ++f)
        dl_set.insert(std::llround(cdir.flow_dl[f] * 100.0));
    std::vector<double> all_de, all_dl;
    for (auto x : de_set) all_de.push_back(x / 100.0);
    for (auto x : dl_set) all_dl.push_back(x / 100.0);
    // Drop max value (splitting at max puts 100% on lo -> no-op!)
    if (all_de.size() > 1) all_de.pop_back();
    if (all_dl.size() > 1) all_dl.pop_back();
    pal.de_vals = all_de;
    pal.dl_vals = all_dl;
    // Sample empirical wait-time quantiles + baseline U95 on 8 train streams
    Tree sttf; sttf.n_nodes = 1;
    double mid_de = all_de.empty() ? 50.0 : all_de[all_de.size() / 2];
    sttf.nodes[1] = {2, true, mid_de, 0, 0};
    std::vector<double> w_samples;
    double sum_u95 = 0.0; int ok = 0;
    for (int k = 0; k < std::min(8, (int)train_streams.size()); ++k) {
        auto m = run_sim(cd, cdir, train_streams[k], ref_n, sttf, -1, false, &w_samples);
        if (!m.dq) { sum_u95 += m.u_95; ++ok; }
    }
    double est_u95 = (ok > 0) ? (sum_u95 / ok) : 5.0;
    std::sort(w_samples.begin(), w_samples.end());
    auto qval = [&](double p) -> double {
        if (w_samples.empty()) return p * 5.0;
        double v = w_samples[std::min(w_samples.size() - 1, (size_t)(p * w_samples.size()))];
        return std::round(v * 20.0) / 20.0;
    };
    // Build w candidate grid from quantiles + triage targets (est_u95 - empty_travel)
    std::set<long long> w_min_set, w_max_set;
    for (double v : {0.35, 0.55, 0.75, 1.00, 1.40, 1.90, 2.45, 3.20, 4.20}) w_min_set.insert(std::llround(v * 100));
    for (double p : {0.15, 0.30, 0.45, 0.60, 0.75, 0.85}) {
        double v = std::max(0.20, qval(p));
        w_min_set.insert(std::llround(v * 100));
    }
    for (double frac : {0.20, 0.40, 0.65}) {
        double d_e = all_de.empty() ? 50.0 : all_de[(size_t)(frac * (all_de.size() - 1))];
        double te = d_e / cd.v_empty + 0.5;
        double cut = std::max(0.25, std::round((est_u95 - te) * 20.0) / 20.0);
        w_min_set.insert(std::llround(cut * 100));
    }
    for (double p : {0.40, 0.65, 0.80, 0.90, 0.95}) {
        double v = std::max(0.50, qval(p));
        w_max_set.insert(std::llround(v * 100));
    }
    // Pick evenly spaced subset of de/dl for core palette (up to 7 de, 5 dl) and all for full (up to 12 de, 8 dl)
    auto pick_sub = [](const std::vector<double>& v, size_t cap) {
        if (v.size() <= cap) return v;
        std::vector<double> r;
        for (size_t k = 0; k < cap; ++k) {
            size_t idx = (k * (v.size() - 1)) / (cap - 1);
            if (r.empty() || std::abs(r.back() - v[idx]) > 1e-6) r.push_back(v[idx]);
        }
        return r;
    };
    auto de_core = pick_sub(all_de, 7);
    auto dl_core = pick_sub(all_dl, 5);
    auto de_full = pick_sub(all_de, 12);
    auto dl_full = pick_sub(all_dl, 8);
    std::vector<double> w_min_all, w_max_all;
    for (auto x : w_min_set) w_min_all.push_back(x / 100.0);
    for (auto x : w_max_set) w_max_all.push_back(x / 100.0);
    auto w_min_core = pick_sub(w_min_all, 6);
    auto w_max_core = pick_sub(w_max_all, 3);
    // Core splits (always includes BOTH urg max 0.5 AND urg min 0.5!)
    pal.core.push_back({0, false, 0.5});
    pal.core.push_back({0, true, 0.5});
    for (double d : de_core) pal.core.push_back({2, true, d});
    for (double d : dl_core) pal.core.push_back({3, true, d});
    if (!dl_core.empty()) pal.core.push_back({3, false, dl_core[dl_core.size() / 2]});
    for (double q : {1.0, 2.0, 3.0}) pal.core.push_back({4, false, q});
    pal.core.push_back({4, true, 1.0});
    for (double w : w_min_core) pal.core.push_back({1, true, w});
    for (double w : w_max_core) pal.core.push_back({1, false, w});
    // Full splits (adds reverse polarities & finer grid)
    pal.full.push_back({0, false, 0.5});
    pal.full.push_back({0, true, 0.5});
    for (double d : de_full) pal.full.push_back({2, true, d});
    for (double d : pick_sub(all_de, 4)) pal.full.push_back({2, false, d});
    for (double d : dl_full) pal.full.push_back({3, true, d});
    for (double d : pick_sub(all_dl, 4)) pal.full.push_back({3, false, d});
    for (double q : {1.0, 2.0, 3.0, 4.0}) pal.full.push_back({4, false, q});
    for (double q : {1.0, 2.0}) pal.full.push_back({4, true, q});
    for (double w : pick_sub(w_min_all, 10)) pal.full.push_back({1, true, w});
    for (double w : pick_sub(w_max_all, 5)) pal.full.push_back({1, false, w});
    return pal;
}
// Find top 3 feasible fleet sizes in [nMin, nMax] using baseline STTF on 24 training seeds
std::vector<std::pair<int, double>> find_best_fleets_sttf(const CaseData& cd, const CompiledDir& cdir,
                                                          const std::vector<StreamPack>& train_streams, int n_seeds = 24) {
    // Pure STTF tree: de min 0 (leaves tie-break directly by de ascending!)
    Tree sttf; sttf.n_nodes = 1; sttf.nodes[1] = {2, true, 0.0, 0, 0};
    std::vector<std::pair<int, double>> res;
    for (int n = cd.n_min; n <= cd.n_max; ++n) {
        auto es = eval_streams(cd, cdir, train_streams, n, sttf, n_seeds, -1);
        if (es.dq_cnt == 0) res.push_back({n, es.mean_cost});
    }
    std::sort(res.begin(), res.end(), [](auto& a, auto& b) { return a.second < b.second; });
    if (res.size() > 3) res.resize(3);
    return res;
}
bool prune_node_strict(const Tree& src, int target_nid, Tree& out, int max_n) {
    if (target_nid <= 1 || target_nid > src.n_nodes) return false;
    Tree c = src;
    for (int p = 1; p <= c.n_nodes; ++p) {
        if (c.nodes[p].lo == target_nid) c.nodes[p].lo = 0;
        if (c.nodes[p].hi == target_nid) c.nodes[p].hi = 0;
    }
    out = canonicalize_tree(c);
    return is_strict_tree(out, max_n);
}
bool insert_above_strict(const Tree& src, int target_nid, const Split& sp, bool old_on_hi, Tree& out, int max_n) {
    if (src.n_nodes >= max_n || target_nid < 1 || target_nid > src.n_nodes) return false;
    Tree c = src;
    int nw = ++c.n_nodes;
    c.nodes[nw] = {sp.feat, sp.is_min, sp.th, old_on_hi ? 0 : target_nid, old_on_hi ? target_nid : 0};
    if (target_nid == 1) {
        std::swap(c.nodes[1], c.nodes[nw]);
        for (int q = 1; q <= c.n_nodes; ++q) {
            if (c.nodes[q].lo == 1) c.nodes[q].lo = nw; else if (c.nodes[q].lo == nw) c.nodes[q].lo = 1;
            if (c.nodes[q].hi == 1) c.nodes[q].hi = nw; else if (c.nodes[q].hi == nw) c.nodes[q].hi = 1;
        }
    } else {
        for (int q = 1; q < nw; ++q) {
            if (c.nodes[q].lo == target_nid) c.nodes[q].lo = nw;
            if (c.nodes[q].hi == target_nid) c.nodes[q].hi = nw;
        }
    }
    out = canonicalize_tree(c);
    return is_strict_tree(out, max_n);
}
// Local Steepest Climb (point edits, grow, flip, parent-child swap, leaf relocate, edge wrap)
Tree local_climb_tree(const CaseData& cd, const CompiledDir& cdir, const std::vector<StreamPack>& train_streams,
                      const Palette& pal, int na, Tree start, double& out_c32, std::mt19937& rng, int max_steps = 5) {
    Tree cur = canonicalize_tree(start);
    double cur32 = eval_streams(cd, cdir, train_streams, na, cur, 32).mean_cost;
    int max_n = cd.max_nodes;
    for (int st = 0; st < max_steps; ++st) {
        std::vector<Tree> prio, extra;
        std::set<std::string> seen; seen.insert(tree_key_str(cur));
        auto add = [&](const Tree& raw, bool is_p) {
            Tree ct = canonicalize_tree(raw);
            if (!is_strict_tree(ct, max_n)) return;
            if (seen.insert(tree_key_str(ct)).second) {
                if (is_p) prio.push_back(ct); else extra.push_back(ct);
            }
        };
        // Point edits + local w nudges
        for (int i = 1; i <= cur.n_nodes; ++i) {
            for (const auto& sp : pal.full) {
                Tree c = cur; c.nodes[i].feat = sp.feat; c.nodes[i].is_min = sp.is_min; c.nodes[i].th = sp.th; add(c, true);
            }
            if (cur.nodes[i].feat == 1) {
                for (double d : {-0.25, -0.10, -0.05, 0.05, 0.10, 0.25}) {
                    Tree c = cur; c.nodes[i].th = std::round(std::max(0.0, cur.nodes[i].th + d) * 100.0) / 100.0; add(c, true);
                }
            }
            Tree cf = cur; std::swap(cf.nodes[i].lo, cf.nodes[i].hi); add(cf, true);
            for (int ch : {cur.nodes[i].lo, cur.nodes[i].hi}) {
                if (!ch) continue;
                Tree cs = cur;
                std::swap(cs.nodes[i].feat, cs.nodes[ch].feat);
                std::swap(cs.nodes[i].is_min, cs.nodes[ch].is_min);
                std::swap(cs.nodes[i].th, cs.nodes[ch].th);
                add(cs, true);
            }
            if (i >= 2 && cur.nodes[i].lo == 0 && cur.nodes[i].hi == 0) {
                Tree pr;
                if (prune_node_strict(cur, i, pr, max_n)) {
                    add(pr, true);
                    for (int p = 1; p <= pr.n_nodes; ++p) {
                        for (int br = 0; br < 2; ++br) {
                            if ((br == 0 ? pr.nodes[p].lo : pr.nodes[p].hi) != 0) continue;
                            for (const auto& sp : pal.core) {
                                Tree c = pr; int nw = ++c.n_nodes;
                                c.nodes[nw] = {sp.feat, sp.is_min, sp.th, 0, 0};
                                if (br == 0) c.nodes[p].lo = nw; else c.nodes[p].hi = nw;
                                add(c, false);
                            }
                        }
                    }
                }
            }
        }
        if (cur.n_nodes < max_n) {
            for (int p = 1; p <= cur.n_nodes; ++p) {
                for (int br = 0; br < 2; ++br) {
                    if ((br == 0 ? cur.nodes[p].lo : cur.nodes[p].hi) != 0) continue;
                    for (const auto& sp : pal.full) {
                        Tree c = cur; int nw = ++c.n_nodes;
                        c.nodes[nw] = {sp.feat, sp.is_min, sp.th, 0, 0};
                        if (br == 0) c.nodes[p].lo = nw; else c.nodes[p].hi = nw;
                        add(c, true);
                    }
                }
            }
            for (int tgt = 1; tgt <= cur.n_nodes; ++tgt) {
                for (const auto& sp : pal.core) {
                    Tree w0, w1;
                    if (insert_above_strict(cur, tgt, sp, false, w0, max_n)) add(w0, false);
                    if (insert_above_strict(cur, tgt, sp, true, w1, max_n)) add(w1, false);
                }
            }
        }
        std::shuffle(prio.begin(), prio.end(), rng);
        std::shuffle(extra.begin(), extra.end(), rng);
        std::vector<Tree> nbrs;
        int tp = std::min((int)prio.size(), 110), te = std::min((int)extra.size(), 35);
        nbrs.insert(nbrs.end(), prio.begin(), prio.begin() + tp);
        nbrs.insert(nbrs.end(), extra.begin(), extra.begin() + te);
        if (nbrs.empty()) break;
        double abort_th = (cur32 < 900000.0) ? (cur32 + 25.0) : 5000.0;
        std::vector<double> sc10(nbrs.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 4)
        for (int i = 0; i < (int)nbrs.size(); ++i) {
            sc10[i] = eval_crn_fast(cd, cdir, train_streams, na, nbrs[i], 10, abort_th, 100);
        }
        std::vector<int> ord(nbrs.size());
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return sc10[a] < sc10[b]; });
        bool imp = false;
        for (int k = 0; k < std::min((int)ord.size(), 3); ++k) {
            if (sc10[ord[k]] >= 900000.0) break;
            double c32 = eval_streams(cd, cdir, train_streams, na, nbrs[ord[k]], 32).mean_cost;
            if (c32 < cur32 - 0.10) { cur32 = c32; cur = nbrs[ord[k]]; imp = true; }
        }
        if (!imp) break;
    }
    out_c32 = cur32;
    return cur;
}
// Beam Search (1 -> maxNodes) + Diverse Domain-Informed Seeds + LNS-2 + 200-Seed Coordinate/Pairwise Polish
struct StageBResult {
    int fleet = 18;
    Tree tree;
    double train200_cost = 999999.0;
    double train200_w = 999.0;
    double train200_u95 = 999.0;
    int train200_dq = 999;
};
StageBResult optimize_tree_stage_b(const CaseData& cd, const CompiledDir& cdir,
                                   const std::vector<StreamPack>& train_streams,
                                   int na, uint32_t seed, bool deep_mode) {
    std::mt19937 rng(seed);
    Palette pal = build_dynamic_palette(cd, cdir, train_streams, na);
    int max_n = cd.max_nodes;
    // 1. Beam Search 1 -> maxNodes starting from ALL core splits as roots!
    std::vector<Tree> beam;
    {
        std::vector<Tree> roots;
        for (const auto& sp : pal.core) {
            Tree t; t.n_nodes = 1; t.nodes[1] = {sp.feat, sp.is_min, sp.th, 0, 0};
            roots.push_back(t);
        }
        std::vector<double> rsc(roots.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 2)
        for (int i = 0; i < (int)roots.size(); ++i) {
            rsc[i] = eval_crn_fast(cd, cdir, train_streams, na, roots[i], 10, 5000.0, 120);
        }
        std::vector<int> ord(roots.size());
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return rsc[a] < rsc[b]; });
        int keep = std::min((int)ord.size(), deep_mode ? 5 : 3);
        for (int k = 0; k < keep; ++k) if (rsc[ord[k]] < 900000.0) beam.push_back(roots[ord[k]]);
    }
    int beam_w = deep_mode ? 5 : 3;
    for (int sz = 2; sz <= max_n && !beam.empty(); ++sz) {
        std::vector<Tree> cands;
        std::set<std::string> seen;
        for (const auto& b : beam) {
            for (int p = 1; p <= b.n_nodes; ++p) {
                for (int br = 0; br < 2; ++br) {
                    if ((br == 0 ? b.nodes[p].lo : b.nodes[p].hi) != 0) continue;
                    for (const auto& sp : pal.core) {
                        Tree c = b; int nw = ++c.n_nodes;
                        c.nodes[nw] = {sp.feat, sp.is_min, sp.th, 0, 0};
                        if (br == 0) c.nodes[p].lo = nw; else c.nodes[p].hi = nw;
                        Tree ct = canonicalize_tree(c);
                        if (is_strict_tree(ct, max_n) && seen.insert(tree_key_str(ct)).second) cands.push_back(ct);
                    }
                }
            }
        }
        if (cands.empty()) break;
        if ((int)cands.size() > (deep_mode ? 180 : 90)) {
            std::shuffle(cands.begin(), cands.end(), rng);
            cands.resize(deep_mode ? 180 : 90);
        }
        std::vector<double> sc(cands.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 4)
        for (int i = 0; i < (int)cands.size(); ++i) {
            sc[i] = eval_crn_fast(cd, cdir, train_streams, na, cands[i], 10, 5000.0, 110);
        }
        std::vector<int> ord(cands.size());
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return sc[a] < sc[b]; });
        beam.clear();
        for (int k = 0; k < std::min((int)ord.size(), beam_w); ++k) {
            if (sc[ord[k]] < 900000.0) beam.push_back(cands[ord[k]]);
        }
    }
    // 2. Also inject general structural templates (near/far + qo + urg min/max + w triage) scaled to this case's quantiles!
    std::vector<Tree> init_pool = beam;
    if (!pal.de_vals.empty() && !pal.dl_vals.empty()) {
        double de_mid = pal.de_vals[pal.de_vals.size() / 2];
        double de_q35 = pal.de_vals[(size_t)(0.35 * (pal.de_vals.size() - 1))];
        double dl_mid = pal.dl_vals[pal.dl_vals.size() / 2];
        for (double de_cut : {de_q35, de_mid}) {
            // Template A: de min -> (qo max 1 -> urg max -> w min 2.2) / (dl min -> qo max 1 -> urg min / (w min 0.8 -> urg max))
            Tree tA; tA.n_nodes = 9;
            tA.nodes[1] = {2, true, de_cut, 2, 3};
            tA.nodes[2] = {4, false, 1, 0, 4};
            tA.nodes[3] = {3, true, dl_mid, 5, 0};
            tA.nodes[4] = {0, false, 0.5, 0, 6};
            tA.nodes[5] = {4, false, 1, 7, 8};
            tA.nodes[6] = {1, true, 2.2, 0, 0};
            tA.nodes[7] = {0, true, 0.5, 0, 0};
            tA.nodes[8] = {1, true, 0.8, 9, 0};
            tA.nodes[9] = {0, false, 0.5, 0, 0};
            if (is_strict_tree(tA, max_n)) init_pool.push_back(canonicalize_tree(tA));
            // Template B: de min -> (urg max -> w min 2.5) / (urg min -> (dl min -> qo max 1) / (w min 0.9 -> qo max 1))
            Tree tB; tB.n_nodes = 9;
            tB.nodes[1] = {2, true, de_cut, 2, 3};
            tB.nodes[2] = {0, false, 0.5, 4, 5};
            tB.nodes[3] = {0, true, 0.5, 6, 7};
            tB.nodes[4] = {4, false, 1, 0, 0};
            tB.nodes[5] = {1, true, 2.5, 0, 0};
            tB.nodes[6] = {3, true, dl_mid, 8, 0};
            tB.nodes[7] = {1, true, 0.9, 9, 0};
            tB.nodes[8] = {4, false, 1, 0, 0};
            tB.nodes[9] = {4, false, 1, 0, 0};
            if (is_strict_tree(tB, max_n)) init_pool.push_back(canonicalize_tree(tB));
            // Template C: urg-first triage at near de, queue-balanced at far de
            Tree tC; tC.n_nodes = 8;
            tC.nodes[1] = {2, true, de_cut, 2, 3};
            tC.nodes[2] = {0, false, 0.5, 0, 4};
            tC.nodes[3] = {4, false, 1, 5, 6};
            tC.nodes[4] = {1, true, 2.5, 0, 0};
            tC.nodes[5] = {0, true, 0.5, 7, 0};
            tC.nodes[6] = {1, true, 1.0, 8, 0};
            tC.nodes[7] = {3, true, dl_mid, 0, 0};
            tC.nodes[8] = {0, false, 0.5, 0, 0};
            if (is_strict_tree(tC, max_n)) init_pool.push_back(canonicalize_tree(tC));
            // Template D: low-urgent-share aggressive urgent rescue (e.g. c2 8% urgent)
            Tree tD; tD.n_nodes = 9;
            tD.nodes[1] = {0, false, 0.5, 2, 3};
            tD.nodes[2] = {2, true, de_cut, 4, 5};
            tD.nodes[3] = {2, true, de_cut, 6, 7};
            tD.nodes[4] = {4, false, 1, 0, 0};
            tD.nodes[5] = {3, true, dl_mid, 8, 0};
            tD.nodes[6] = {1, true, 2.5, 0, 0};
            tD.nodes[7] = {1, true, 1.0, 0, 0};
            tD.nodes[8] = {4, false, 1, 0, 9};
            tD.nodes[9] = {1, true, 1.5, 0, 0};
            if (is_strict_tree(tD, max_n)) init_pool.push_back(canonicalize_tree(tD));
        }
    }
    // Evaluate init_pool on 48 seeds and pick top 2 distinct starting trees to climb + LNS!
    std::vector<std::pair<double, Tree>> scored_init;
    std::set<std::string> iseen;
    for (const auto& t : init_pool) {
        if (!iseen.insert(tree_key_str(t)).second) continue;
        double c48 = eval_streams(cd, cdir, train_streams, na, t, 48).mean_cost;
        if (c48 < 900000.0) scored_init.push_back({c48, t});
    }
    std::sort(scored_init.begin(), scored_init.end(), [](auto& a, auto& b) { return a.first < b.first; });
    if (scored_init.empty()) {
        Tree sttf; sttf.n_nodes = 1; sttf.nodes[1] = {2, true, 0.0, 0, 0};
        scored_init.push_back({eval_streams(cd, cdir, train_streams, na, sttf, 48).mean_cost, sttf});
    }
    Tree best = scored_init[0].second;
    double best32 = scored_init[0].first;
    int n_climb_starts = std::min((int)scored_init.size(), deep_mode ? 2 : 1);
    for (int s = 0; s < n_climb_starts; ++s) {
        double c32 = 999999.0;
        Tree cl = local_climb_tree(cd, cdir, train_streams, pal, na, scored_init[s].second, c32, rng, deep_mode ? 5 : 3);
        if (c32 < best32) { best32 = c32; best = cl; }
    }
    // 3. LNS-2/3 Destroy-and-Regrow + Steepest Climb
    int n_kicks = deep_mode ? 3 : 1;
    Tree cur = best; double cur32 = best32;
    for (int k = 0; k < n_kicks; ++k) {
        std::vector<Tree> kicks;
        std::set<std::string> ks;
        for (int tr = 0; tr < 45 && (int)kicks.size() < 10; ++tr) {
            Tree src = (tr % 3 == 0) ? cur : best;
            int kd = 2 + (rng() % 2);
            for (int d = 0; d < kd && src.n_nodes > 2; ++d) {
                std::vector<int> cands;
                for (int i = 2; i <= src.n_nodes; ++i)
                    if (d == 0 || (src.nodes[i].lo == 0 && src.nodes[i].hi == 0)) cands.push_back(i);
                if (cands.empty()) break;
                Tree pr;
                if (prune_node_strict(src, cands[rng() % cands.size()], pr, max_n)) src = pr;
            }
            if ((rng() % 2) == 0 && src.n_nodes >= 2) {
                int idx = 1 + (rng() % src.n_nodes);
                for (int t2 = 0; t2 < 10; ++t2) {
                    const auto& sp = pal.full[rng() % pal.full.size()];
                    Tree c = src; c.nodes[idx].feat = sp.feat; c.nodes[idx].is_min = sp.is_min; c.nodes[idx].th = sp.th;
                    if (is_strict_tree(c, max_n)) { src = canonicalize_tree(c); break; }
                }
            }
            int target_n = std::min(max_n, 7 + (int)(rng() % 3));
            for (int t2 = 0; t2 < 40 && src.n_nodes < target_n; ++t2) {
                std::vector<std::pair<int, int>> slots;
                for (int p = 1; p <= src.n_nodes; ++p) {
                    if (src.nodes[p].lo == 0) slots.push_back({p, 0});
                    if (src.nodes[p].hi == 0) slots.push_back({p, 1});
                }
                if (slots.empty()) break;
                auto [p, br] = slots[rng() % slots.size()];
                const auto& sp = pal.core[rng() % pal.core.size()];
                Tree c = src; int nw = ++c.n_nodes;
                c.nodes[nw] = {sp.feat, sp.is_min, sp.th, 0, 0};
                if (br == 0) c.nodes[p].lo = nw; else c.nodes[p].hi = nw;
                if (is_strict_tree(c, max_n)) src = canonicalize_tree(c);
            }
            if (is_strict_tree(src, max_n) && ks.insert(tree_key_str(src)).second) kicks.push_back(src);
        }
        if (kicks.empty()) continue;
        std::vector<double> ksc(kicks.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 2)
        for (int i = 0; i < (int)kicks.size(); ++i) {
            ksc[i] = eval_crn_fast(cd, cdir, train_streams, na, kicks[i], 10, best32 + 30.0, 100);
        }
        int bi = (int)(std::min_element(ksc.begin(), ksc.end()) - ksc.begin());
        if (ksc[bi] < 900000.0) {
            double c32 = 999999.0;
            Tree cl = local_climb_tree(cd, cdir, train_streams, pal, na, kicks[bi], c32, rng, deep_mode ? 4 : 3);
            if (c32 < best32 - 0.08) { best32 = c32; best = cl; cur32 = c32; cur = cl; }
            else if (c32 < cur32 + 3.0) { cur32 = c32; cur = cl; }
        }
    }
    // 4. Full 200-Seed CRN Coordinate Polish (all nodes + fine w 0.05-step + 2D w-pair polish in deep_mode)
    auto best_es = eval_streams(cd, cdir, train_streams, na, best, (int)train_streams.size());
    if (deep_mode && best_es.dq_cnt == 0) {
        // 4a. 1-node full-palette discrete polish on 200 seeds
        for (int nid = 1; nid <= best.n_nodes; ++nid) {
            std::vector<Tree> cands;
            std::set<std::string> seen; seen.insert(tree_key_str(best));
            for (const auto& sp : pal.full) {
                Tree c = best; c.nodes[nid].feat = sp.feat; c.nodes[nid].is_min = sp.is_min; c.nodes[nid].th = sp.th;
                Tree ct = canonicalize_tree(c);
                if (is_strict_tree(ct, max_n) && seen.insert(tree_key_str(ct)).second) cands.push_back(ct);
            }
            if (cands.empty()) continue;
            std::vector<double> sc24(cands.size(), 999999.0);
            #pragma omp parallel for schedule(dynamic, 2)
            for (int i = 0; i < (int)cands.size(); ++i)
                sc24[i] = eval_crn_fast(cd, cdir, train_streams, na, cands[i], 24, best_es.mean_cost + 15.0, 120);
            std::vector<int> ord(cands.size());
            std::iota(ord.begin(), ord.end(), 0);
            std::sort(ord.begin(), ord.end(), [&](int a, int b) { return sc24[a] < sc24[b]; });
            for (int k = 0; k < std::min((int)ord.size(), 2); ++k) {
                if (sc24[ord[k]] >= 900000.0) break;
                auto es = eval_streams(cd, cdir, train_streams, na, cands[ord[k]], (int)train_streams.size());
                if (es.dq_cnt == 0 && es.mean_cost < best_es.mean_cost - 0.08) { best_es = es; best = cands[ord[k]]; }
            }
        }
        // 4b. Fine 1D w-threshold 0.05-step polish on 200 seeds
        for (int pass = 0; pass < 2; ++pass) {
            bool imp = false;
            for (int nid = 1; nid <= best.n_nodes; ++nid) {
                if (best.nodes[nid].feat != 1) continue;
                double orig = best.nodes[nid].th;
                for (int d : {-4, -2, -1, 1, 2, 4}) {
                    double nv = std::round(std::max(0.05, orig + d * 0.05) * 100.0) / 100.0;
                    if (std::abs(nv - orig) < 1e-6) continue;
                    Tree c = best; c.nodes[nid].th = nv;
                    auto es = eval_streams(cd, cdir, train_streams, na, c, (int)train_streams.size());
                    if (es.dq_cnt == 0 && es.mean_cost < best_es.mean_cost - 0.05) {
                        best_es = es; best = c; imp = true;
                    }
                }
            }
            if (!imp) break;
        }
        // 4c. 2D pairwise w-threshold polish if tree has >= 2 w nodes
        std::vector<int> w_ids;
        for (int nid = 1; nid <= best.n_nodes; ++nid) if (best.nodes[nid].feat == 1) w_ids.push_back(nid);
        if (w_ids.size() >= 2) {
            int i1 = w_ids[0], i2 = w_ids[1];
            double o1 = best.nodes[i1].th, o2 = best.nodes[i2].th;
            for (double d1 : {-0.10, 0.10}) {
                for (double d2 : {-0.10, 0.10}) {
                    Tree c = best;
                    c.nodes[i1].th = std::round(std::max(0.05, o1 + d1) * 100.0) / 100.0;
                    c.nodes[i2].th = std::round(std::max(0.05, o2 + d2) * 100.0) / 100.0;
                    auto es = eval_streams(cd, cdir, train_streams, na, c, (int)train_streams.size());
                    if (es.dq_cnt == 0 && es.mean_cost < best_es.mean_cost - 0.05) {
                        best_es = es; best = c;
                    }
                }
            }
        }
    }
    return StageBResult{na, best, best_es.mean_cost, best_es.mean_w, best_es.mean_u95, best_es.dq_cnt};
}
// ============================================================================
// 8. CSV Export & Full Constraint/Brute-Force Verification
// ============================================================================
void save_solution_csv(const std::string& path, const std::string& dir, int fleet, const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ofstream out(path, std::ios::binary);
    out << "dir," << dir << "\r\n";
    out << "fleet," << fleet << "\r\n";
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        out << "key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ",";
        if (tn.feat == 0) out << "0.5";
        else if (std::abs(tn.th - std::round(tn.th)) < 1e-9) out << (long long)std::llround(tn.th);
        else out << std::fixed << std::setprecision(2) << tn.th;
        out << "," << tn.lo << "," << tn.hi << "\r\n";
    }
}
void print_tree(const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        std::cout << "    key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ",";
        if (tn.feat == 0) std::cout << "0.5";
        else if (std::abs(tn.th - std::round(tn.th)) < 1e-9) std::cout << (long long)std::llround(tn.th);
        else std::cout << std::fixed << std::setprecision(2) << tn.th;
        std::cout << "," << tn.lo << "," << tn.hi << "\n";
    }
}
double verify_fast_vs_brute(const CaseData& cd, const CompiledDir& cdir,
                            const std::vector<StreamPack>& streams, int fleet, const Tree& t, int n_check = 10) {
    double max_err = 0.0;
    int N = std::min(n_check, (int)streams.size());
    for (int i = 0; i < N; ++i) {
        auto m1 = run_sim(cd, cdir, streams[i], fleet, t, -1, false);
        auto m2 = run_sim(cd, cdir, streams[i], fleet, t, -1, true);
        if (m1.dq != m2.dq) return 999999.0;
        if (!m1.dq) max_err = std::max(max_err, std::abs(m1.cost - m2.cost));
    }
    return max_err;
}
// ============================================================================
// 9. CLI Steps (Step 1 Verification | Full Pipeline for Single/All Cases)
// ============================================================================
void run_step1_verify(const std::vector<std::pair<std::string, std::string>>& cases) {
    std::cout << "================================================================================\n";
    std::cout << "  STEP 1: SCHEMA PARSING, GRAPH/SCC/CORRIDOR & C1 BASELINE VERIFICATION\n";
    std::cout << "================================================================================\n";
    for (const auto& [cname, path] : cases) {
        CaseData cd = load_case(path, cname);
        bool z0_ok = is_valid_dir(cd, cd.z0);
        CompiledDir cz0 = compile_dir(cd, cd.z0, true);
        std::string active_dir = cd.fixed_dir.empty() ? cd.z0 : cd.fixed_dir;
        bool act_ok = is_valid_dir(cd, active_dir);
        CompiledDir cact = compile_dir(cd, active_dir, true);
        std::cout << "\n[" << cname << "] " << cd.title << "\n"
                  << "  V=" << cd.n_nodes << " E=" << cd.n_edges << " S=" << cd.n_stations
                  << " F=" << cd.n_flows << " sumFlow=" << cd.total_flow << " lam=" << std::fixed << std::setprecision(1) << cd.lam_hr
                  << " urgDest=" << cd.urgent_dest << " n=[" << cd.n_min << ".." << cd.n_max << "]"
                  << " fixedDir=" << (cd.fixed_dir.empty() ? "FREE" : "LOCKED")
                  << " parLanes=" << cd.n_parallel_pairs << " corridors=" << cd.corridors.size() << "\n"
                  << "  Junctions=" << cd.n_junctions
                  << (cd.meta_junctions >= 0 ? (" (meta=" + std::to_string(cd.meta_junctions) + (cd.n_junctions == cd.meta_junctions ? " MATCH!" : " DIFF!") + ")") : "")
                  << " | z0 SCC=" << (z0_ok ? "OK" : "FAIL")
                  << " | sumFD(z0)=" << std::setprecision(1) << cz0.sum_fd
                  << (cd.meta_sum_fd_z0 > 0 ? (" (meta=" + std::to_string(cd.meta_sum_fd_z0).substr(0, 8) +
                      (std::abs(cz0.sum_fd - cd.meta_sum_fd_z0) < 0.5 ? " MATCH!)" : " DIFF!)")) : "")
                  << " | active_dir SCC=" << (act_ok ? "OK" : "FAIL") << " sumFD=" << cact.sum_fd << "\n";
        if (cname == "c1") {
            Tree sttf; sttf.n_nodes = 1; sttf.nodes[1] = {2, true, 75.0, 0, 0};
            auto pub_streams = build_streams(cd, cd.seeds);
            double bf_err = verify_fast_vs_brute(cd, cact, pub_streams, 18, sttf, 5);
            std::cout << "  --- c1.json Baseline Verification (fixedDir, fleet=18, key,1,de,min,75,0,0, seeds=[11..15]) ---\n";
            double sc = 0, sw = 0, su = 0; int dq = 0;
            for (int i = 0; i < (int)cd.seeds.size(); ++i) {
                auto m = run_sim(cd, cact, pub_streams[i], 18, sttf);
                std::cout << "    Seed " << cd.seeds[i] << ": Cost=" << std::setprecision(2) << m.cost
                          << " | W_bar=" << std::setprecision(4) << m.w_bar
                          << " | U95=" << m.u_95
                          << " | maxQ=" << m.max_q << " endQ=" << m.end_q
                          << " | DQ=" << (m.dq ? "YES" : "NO") << "\n";
                if (m.dq) ++dq; else { sc += m.cost; sw += m.w_bar; su += m.u_95; }
            }
            int ok = (int)cd.seeds.size() - dq;
            std::cout << "    5-Seed Mean: Cost=" << std::setprecision(2) << (ok ? sc / ok : 999999.0)
                      << " | W_bar=" << std::setprecision(4) << (ok ? sw / ok : 999.0)
                      << " | U95=" << (ok ? su / ok : 999.0)
                      << " | DQ=" << dq << "/5 | fast-vs-brute err=" << std::setprecision(9) << bf_err << "\n";
        }
    }
    std::cout << "\nSTEP 1 VERIFICATION COMPLETE.\n" << std::flush;
}
struct FinalCandidate {
    std::string dir;
    CompiledDir cdir;
    int fleet;
    Tree tree;
    double train200;
    EvalSummary val500;
    EvalSummary pub5;
    double bf_err;
};
void solve_case_full(const std::string& cname, const std::string& json_path, const std::string& out_csv) {
    auto T0 = std::chrono::steady_clock::now();
    CaseData cd = load_case(json_path, cname);
    std::cout << "\n================================================================================\n";
    std::cout << "  SOLVING [" << cname << "] " << cd.title << " (V=" << cd.n_nodes << ", E=" << cd.n_edges
              << ", S=" << cd.n_stations << ", F=" << cd.n_flows << ", n=[" << cd.n_min << ".." << cd.n_max << "])\n";
    std::cout << "================================================================================\n" << std::flush;
    // Generate CRN streams: train 1001..1200 (200), val 2001..2500 (500), pub [11..15] (5)
    std::vector<int> tr_seeds(200), val_seeds(500);
    std::iota(tr_seeds.begin(), tr_seeds.end(), 1001);
    std::iota(val_seeds.begin(), val_seeds.end(), 2001);
    auto tr_streams = build_streams(cd, tr_seeds);
    auto val_streams = build_streams(cd, val_seeds);
    auto pub_streams = build_streams(cd, cd.seeds);
    // STAGE A: Surrogate-based Top K=15 diverse dir candidates
    auto dir_cands = optimize_dirs_stage_a(cd, 15);
    // STAGE B Phase 1: Screen all K dir candidates with STTF fleet search + quick tree optimization
    std::cout << "  [Stage B-1] Screening " << dir_cands.size() << " dir candidates with STTF fleet sweep...\n" << std::flush;
    struct DirFleetSeed {
        int dir_idx;
        int best_n;
        std::vector<int> top_ns;
        double sttf_cost;
    };
    std::vector<DirFleetSeed> dfs_list;
    for (int di = 0; di < (int)dir_cands.size(); ++di) {
        auto top_f = find_best_fleets_sttf(cd, dir_cands[di], tr_streams, 24);
        if (top_f.empty()) continue;
        std::vector<int> ns;
        for (auto [n, c] : top_f) ns.push_back(n);
        dfs_list.push_back({di, top_f[0].first, ns, top_f[0].second});
        std::cout << "    dir#" << std::setw(2) << di << ": sumFD=" << std::fixed << std::setprecision(1) << dir_cands[di].sum_fd
                  << " empLP=" << dir_cands[di].empty_lp << " jMax=" << dir_cands[di].junc_max
                  << " | STTF best n=" << top_f[0].first << " cost24=" << std::setprecision(2) << top_f[0].second
                  << " (top3 n:";
        for (int n : ns) std::cout << " " << n;
        std::cout << ")\n" << std::flush;
    }
    std::sort(dfs_list.begin(), dfs_list.end(), [](auto& a, auto& b) { return a.sttf_cost < b.sttf_cost; });
    // STAGE B Phase 2: Quick Beam+LNS on top 6 dirs (at their best fleet) to rank true DES quality,
    // then DEEP Beam+LNS+200-seed Polish on top 3 dirs across their top 2 fleet sizes!
    int n_quick = std::min((int)dfs_list.size(), cd.fixed_dir.empty() ? 6 : 1);
    std::vector<std::pair<double, int>> dir_des_rank;
    if (n_quick > 1) {
        std::cout << "  [Stage B-2] Quick Tree Opt (Beam+LNS) on top " << n_quick << " dirs to rank real DES potential...\n" << std::flush;
        for (int qi = 0; qi < n_quick; ++qi) {
            int di = dfs_list[qi].dir_idx;
            int na = dfs_list[qi].best_n;
            auto res = optimize_tree_stage_b(cd, dir_cands[di], tr_streams, na, 9001 + di * 37, false);
            dir_des_rank.push_back({res.train200_cost, qi});
            std::cout << "    dir#" << std::setw(2) << di << " (n=" << na << "): quick train200="
                      << std::fixed << std::setprecision(2) << res.train200_cost
                      << " (w=" << std::setprecision(4) << res.train200_w
                      << " u95=" << res.train200_u95 << " dq=" << res.train200_dq << ")\n" << std::flush;
        }
        std::sort(dir_des_rank.begin(), dir_des_rank.end());
    } else if (!dfs_list.empty()) {
        dir_des_rank.push_back({dfs_list[0].sttf_cost, 0});
    }
    int n_deep_dirs = std::min((int)dir_des_rank.size(), cd.fixed_dir.empty() ? 3 : 1);
    std::cout << "  [Stage B-3] DEEP Beam+LNS+200-Seed Polish on top " << n_deep_dirs << " dir(s) x top fleet sizes...\n" << std::flush;
    std::vector<FinalCandidate> pool;
    std::set<std::string> pool_keys;
    auto add_candidate = [&](const CompiledDir& cdir, int na, const Tree& raw_t, double tr200) {
        Tree t = canonicalize_tree(raw_t);
        if (!is_strict_tree(t, cd.max_nodes)) return;
        if (na < cd.n_min || na > cd.n_max) return;
        std::string key = cdir.dir + "|" + std::to_string(na) + "|" + tree_key_str(t);
        if (!pool_keys.insert(key).second) return;
        pool.push_back(FinalCandidate{cdir.dir, cdir, na, t, tr200, EvalSummary{}, EvalSummary{}, 0.0});
    };
    for (int rk = 0; rk < n_deep_dirs; ++rk) {
        const auto& df = dfs_list[dir_des_rank[rk].second];
        const auto& cdir = dir_cands[df.dir_idx];
        // Optimize across top 2 fleet sizes (top 3 if fixedDir)
        int n_fleets = std::min((int)df.top_ns.size(), cd.fixed_dir.empty() ? 2 : 3);
        for (int fi = 0; fi < n_fleets; ++fi) {
            int na = df.top_ns[fi];
            auto res = optimize_tree_stage_b(cd, cdir, tr_streams, na, 20001 + rk * 101 + fi * 17, true);
            std::cout << "    [DEEP dir#" << df.dir_idx << " n=" << na << "] train200=" << std::fixed << std::setprecision(2)
                      << res.train200_cost << " (w=" << std::setprecision(4) << res.train200_w
                      << " u95=" << res.train200_u95 << " dq=" << res.train200_dq << ")\n" << std::flush;
            add_candidate(cdir, na, res.tree, res.train200_cost);
            // Re-adjust surrounding fleet sizes {n-1, n, n+1} on discovered tree!
            for (int dn : {-1, 1}) {
                int nn = na + dn;
                if (nn < cd.n_min || nn > cd.n_max) continue;
                auto es = eval_streams(cd, cdir, tr_streams, nn, res.tree, 200);
                if (es.dq_cnt == 0) {
                    add_candidate(cdir, nn, res.tree, es.mean_cost);
                }
            }
        }
    }
    // STAGE 3: Out-of-Sample Validation on 500 Unseen CRN Seeds (2001..2500) + 5 Public Seeds (11..15)
    std::cout << "  [Step 3] Out-of-Sample Validation (" << pool.size() << " candidates on 500 val seeds [2001..2500] + 5 pub seeds [11..15])...\n" << std::flush;
    std::vector<FinalCandidate> survivors;
    for (auto& cand : pool) {
        cand.val500 = eval_streams(cd, cand.cdir, val_streams, cand.fleet, cand.tree, 500);
        cand.pub5   = eval_streams(cd, cand.cdir, pub_streams, cand.fleet, cand.tree, (int)cd.seeds.size());
        bool pass = (cand.val500.dq_cnt == 0 && cand.pub5.dq_cnt == 0);
        std::cout << "    n=" << std::setw(2) << cand.fleet << " | tr200=" << std::fixed << std::setprecision(2) << cand.train200
                  << " | val500 Mean=" << cand.val500.mean_cost << " P95=" << cand.val500.p95_cost
                  << " Robust=" << cand.val500.robust_score << " (w=" << std::setprecision(4) << cand.val500.mean_w
                  << " u95=" << cand.val500.mean_u95 << " dq=" << cand.val500.dq_cnt << "/500)"
                  << " | pub5=" << std::setprecision(2) << cand.pub5.mean_cost << " (dq=" << cand.pub5.dq_cnt << "/5)"
                  << (pass ? " [PASS]" : " [REJECT]") << "\n" << std::flush;
        if (pass) survivors.push_back(cand);
    }
    if (survivors.empty()) {
        std::cerr << "ERROR: No surviving candidate for " << cname << "!\n";
        return;
    }
    std::sort(survivors.begin(), survivors.end(), [](const FinalCandidate& a, const FinalCandidate& b) {
        return a.val500.robust_score < b.val500.robust_score;
    });
    auto& win = survivors[0];
    win.bf_err = verify_fast_vs_brute(cd, win.cdir, pub_streams, win.fleet, win.tree, 5);
    bool scc_ok = is_valid_dir(cd, win.dir);
    bool tree_ok = is_strict_tree(win.tree, cd.max_nodes);
    bool fleet_ok = (win.fleet >= cd.n_min && win.fleet <= cd.n_max);
    save_solution_csv(out_csv, win.dir, win.fleet, win.tree);
    auto T1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(T1 - T0).count();
    std::cout << "\n  >>> FINAL WINNER [" << cname << "] saved to " << out_csv << " (" << std::setprecision(1) << sec << "s) <<<\n"
              << "    Checks: dir_SCC=" << (scc_ok ? "OK" : "FAIL")
              << " | fleet=" << win.fleet << (fleet_ok ? "(OK)" : "(FAIL)")
              << " | tree=" << win.tree.n_nodes << "n" << (tree_ok ? "(OK)" : "(FAIL)")
              << " | fast-vs-brute=" << std::setprecision(9) << win.bf_err << "\n"
              << "    val500 (2001..2500): Mean=" << std::setprecision(2) << win.val500.mean_cost
              << " +/- " << win.val500.std_cost << " | P95=" << win.val500.p95_cost
              << " | Robust=" << win.val500.robust_score
              << " | W=" << std::setprecision(4) << win.val500.mean_w
              << " U95=" << win.val500.mean_u95
              << " | maxQ=" << win.val500.max_q << " maxEndQ=" << win.val500.max_end_q << " dq=0/500\n"
              << "    pub5   (11..15)    : Mean=" << std::setprecision(2) << win.pub5.mean_cost
              << " | W=" << std::setprecision(4) << win.pub5.mean_w
              << " U95=" << win.pub5.mean_u95 << " dq=0/5\n"
              << "    dir," << win.dir << "\n"
              << "    fleet," << win.fleet << "\n";
    print_tree(win.tree);
    std::cout << std::flush;
}
std::tuple<std::string, int, Tree> load_solution_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open CSV: " << path << "\n"; std::exit(1); }
    std::string line, dir;
    int fleet = 18, max_id = 0;
    Tree t;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> p;
        while (std::getline(ss, tok, ',')) p.push_back(tok);
        if (p.empty()) continue;
        if (p[0] == "dir" && p.size() >= 2) dir = p[1];
        else if (p[0] == "fleet" && p.size() >= 2) fleet = std::stoi(p[1]);
        else if (p[0] == "key" && p.size() >= 7) {
            int nid = std::stoi(p[1]);
            max_id = std::max(max_id, nid);
            int f = -1;
            for (int i = 0; i < 5; ++i) if (p[2] == FEAT_NAMES[i]) f = i;
            t.nodes[nid] = TreeNode{f, p[3] == "min", std::stod(p[4]), std::stoi(p[5]), std::stoi(p[6])};
        }
    }
    t.n_nodes = max_id;
    return {dir, fleet, t};
}
void verify_and_eval_csv(const std::string& cname, const std::string& json_path, const std::string& csv_path, bool full_eval) {
    CaseData cd = load_case(json_path, cname);
    auto [dir, fleet, tree] = load_solution_csv(csv_path);
    bool dir_ok = is_valid_dir(cd, dir);
    bool fleet_ok = (fleet >= cd.n_min && fleet <= cd.n_max);
    bool tree_ok = is_strict_tree(tree, cd.max_nodes);
    if (!dir_ok || !fleet_ok || !tree_ok) {
        std::cout << "[FAIL] " << csv_path << " dir=" << (dir_ok ? "OK" : "FAIL")
                  << " fleet=" << fleet << (fleet_ok ? "(OK)" : "(FAIL)")
                  << " tree=" << tree.n_nodes << "n" << (tree_ok ? "(OK)" : "(FAIL)") << "\n";
        std::exit(1);
    }
    CompiledDir cdir = compile_dir(cd, dir, true);
    auto pub_streams = build_streams(cd, cd.seeds);
    double bf_err = verify_fast_vs_brute(cd, cdir, pub_streams, fleet, tree, (int)cd.seeds.size());
    auto pub5 = eval_streams(cd, cdir, pub_streams, fleet, tree);
    std::cout << "[PASS] " << csv_path << " (" << cname << "): dir_SCC=OK parLanes=OK fleet=" << fleet
              << " in [" << cd.n_min << ".." << cd.n_max << "] tree=" << tree.n_nodes << "n strict=OK"
              << " | fast-vs-brute=" << std::fixed << std::setprecision(9) << bf_err
              << " | pub5=" << std::setprecision(2) << pub5.mean_cost << " (dq=" << pub5.dq_cnt << "/5)";
    if (full_eval) {
        std::vector<int> val_seeds(500);
        std::iota(val_seeds.begin(), val_seeds.end(), 2001);
        auto val_streams = build_streams(cd, val_seeds);
        auto val500 = eval_streams(cd, cdir, val_streams, fleet, tree, 500);
        std::cout << " | val500 Mean=" << val500.mean_cost << " +/- " << val500.std_cost
                  << " P95=" << val500.p95_cost << " Robust=" << val500.robust_score
                  << " (W=" << std::setprecision(4) << val500.mean_w << " U95=" << val500.mean_u95
                  << " maxQ=" << val500.max_q << " endQ=" << val500.max_end_q << " dq=" << val500.dq_cnt << "/500)";
    }
    std::cout << "\n" << std::flush;
}
int main(int argc, char** argv) {
    #ifdef _OPENMP
    std::cout << "agv_solver C++20 + OpenMP (threads=" << omp_get_max_threads() << ")\n" << std::flush;
    #else
    std::cout << "agv_solver C++20 (single-threaded)\n" << std::flush;
    #endif
    std::vector<std::pair<std::string, std::string>> all_cases = {
        {"c1", "c1.json"},
        {"c2", "c2.json"},
        {"c3", "c3.json"},
        {"c4", "c4.json"},
        {"c5", "c5.json"},
    };
    std::string mode = (argc >= 2) ? argv[1] : "--step1";
    if (mode == "--step1") {
        run_step1_verify(all_cases);
    } else if (mode == "--solve-all") {
        for (const auto& [cname, jpath] : all_cases) {
            solve_case_full(cname, jpath, "ans_" + cname + ".csv");
        }
    } else if (mode == "--solve" && argc >= 3) {
        std::string target = argv[2];
        std::string out_csv = (argc >= 4) ? argv[3] : ("ans_" + target + ".csv");
        for (const auto& [cname, jpath] : all_cases) {
            if (cname == target) solve_case_full(cname, jpath, out_csv);
        }
    } else if (mode == "--verify" || mode == "--eval") {
        bool full_ev = (mode == "--eval");
        if (argc == 2) {
            for (const auto& [cname, jpath] : all_cases) {
                verify_and_eval_csv(cname, jpath, "ans_" + cname + ".csv", full_ev);
            }
        } else if (argc >= 4) {
            std::string target = argv[2], csv_p = argv[3];
            std::string jpath = target.ends_with(".json") ? target : (target + ".json");
            verify_and_eval_csv(target, jpath, csv_p, full_ev);
        } else {
            std::cerr << "Usage: agv_solver.exe --verify [c1..c5 ans.csv] | --eval [c1..c5 ans.csv]\n";
            return 1;
        }
    } else {
        std::cerr << "Usage: agv_solver.exe [--step1 | --solve c1..c5 [out.csv] | --solve-all | --verify [case csv] | --eval [case csv]]\n";
        return 1;
    }
    return 0;
}