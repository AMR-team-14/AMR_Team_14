#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
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

    std::array<int, 7> pd_nodes{};
    std::array<std::string, 7> depts{};
    std::array<int, 26> is_inter{};
    std::array<std::array<double, 26>, 26> sp_mat{};
    std::vector<FlowDef> flows;
    std::vector<Hop> comp_path[26][26][2];
    std::vector<std::vector<Arrival>> streams;
    std::vector<std::vector<Event>> base_events;
};

static inline double round9(double x) {
    return std::llround(x * 1e9) / 1e9;
}

static const char* FEAT_NAMES[5] = {"urg", "w", "de", "dl", "qo"};

int feat_from_str(const std::string& s) {
    for (int i = 0; i < 5; ++i) {
        if (s == FEAT_NAMES[i]) return i;
    }
    return -1;
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

namespace json_util {
    std::string read_all(const std::string& path) {
        std::ifstream f(path);
        if (!f) { std::cerr << "Cannot open " << path << "\n"; std::exit(1); }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    size_t find_key_pos(const std::string& s, const std::string& key) {
        std::string pat = "\"" + key + "\"";
        size_t pos = s.find(pat);
        if (pos == std::string::npos) { std::cerr << "Missing JSON key: " << key << "\n"; std::exit(1); }
        pos = s.find(':', pos + pat.size());
        return pos + 1;
    }

    double parse_num(const std::string& s, const std::string& key) {
        size_t p = find_key_pos(s, key);
        while (p < s.size() && (std::isspace((unsigned char)s[p]))) ++p;
        return std::stod(s.substr(p));
    }

    std::string parse_str(const std::string& s, const std::string& key) {
        size_t p = find_key_pos(s, key);
        size_t q1 = s.find('"', p);
        size_t q2 = s.find('"', q1 + 1);
        return s.substr(q1 + 1, q2 - q1 - 1);
    }

    std::string extract_bracket(const std::string& s, size_t start_pos) {
        size_t p = s.find('[', start_pos);
        int depth = 0;
        bool in_str = false;
        for (size_t i = p; i < s.size(); ++i) {
            if (s[i] == '"') in_str = !in_str;
            if (in_str) continue;
            if (s[i] == '[') ++depth;
            else if (s[i] == ']') {
                --depth;
                if (depth == 0) return s.substr(p, i - p + 1);
            }
        }
        return "";
    }

    std::vector<double> parse_all_numbers(const std::string& block) {
        std::vector<double> out;
        size_t i = 0;
        while (i < block.size()) {
            if (std::isdigit((unsigned char)block[i]) || block[i] == '-' || block[i] == '.') {
                size_t next = i;
                double v = std::stod(block.substr(i), &next);
                out.push_back(v);
                i += next;
            } else {
                ++i;
            }
        }
        return out;
    }

    std::vector<std::string> parse_all_strings(const std::string& block) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < block.size()) {
            size_t q1 = block.find('"', i);
            if (q1 == std::string::npos) break;
            size_t q2 = block.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            out.push_back(block.substr(q1 + 1, q2 - q1 - 1));
            i = q2 + 1;
        }
        return out;
    }
}

std::vector<Arrival> generate_arrivals(const SimData& sd, uint32_t seed) {
    Mulberry32 rng(seed);
    double total_flow = 0.0;
    for (const auto& fl : sd.flows) total_flow += fl.rate;
    double lam = (total_flow * sd.alpha) / 60.0;

    std::vector<double> cum(sd.flows.size());
    double acc = 0.0;
    for (size_t i = 0; i < sd.flows.size(); ++i) {
        acc += sd.flows[i].rate / total_flow;
        cum[i] = acc;
    }

    std::vector<Arrival> arr;
    double t = 0.0;
    while (true) {
        double u1 = std::max(1e-12, rng.rand());
        t += -std::log(u1) / lam;
        if (t > sd.t_end) break;
        double u2 = rng.rand();
        int f_idx = (int)(std::lower_bound(cum.begin(), cum.end(), u2) - cum.begin());
        if (f_idx >= (int)sd.flows.size()) f_idx = (int)sd.flows.size() - 1;
        int o_node = sd.pd_nodes[sd.flows[f_idx].o_dept];
        int d_node = sd.pd_nodes[sd.flows[f_idx].d_dept];
        int urg = (sd.flows[f_idx].d_dept == sd.urgent_dest) ? 1 : 0;
        arr.push_back(Arrival{t, o_node, d_node, urg, sd.sp_mat[o_node][d_node], f_idx});
    }
    return arr;
}

std::vector<Arrival> load_seed11_csv(const SimData& sd, const std::string& csv_path, const std::map<std::pair<int, int>, int>& flow_map) {
    std::ifstream in(csv_path);
    if (!in) { std::cerr << "Cannot open " << csv_path << "\n"; std::exit(1); }
    std::map<std::string, int> dept_to_node;
    for (int i = 0; i < 7; ++i) dept_to_node[sd.depts[i]] = sd.pd_nodes[i];

    std::string header;
    std::getline(in, header);
    std::vector<std::string> cols;
    {
        std::stringstream ss(header);
        std::string tok;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);
    }
    auto col_idx = [&](const std::string& name) -> int {
        for (int i = 0; i < (int)cols.size(); ++i) if (cols[i] == name) return i;
        return -1;
    };
    int i_id = col_idx("id"), i_arr = col_idx("arrive"), i_from = col_idx("from"), i_to = col_idx("to"), i_urg = col_idx("urg");

    struct Row { int id; Arrival ar; };
    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> p;
        while (std::getline(ss, tok, ',')) p.push_back(tok);
        if ((int)p.size() <= std::max({i_id, i_arr, i_from, i_to, i_urg})) continue;
        int id = std::stoi(p[i_id]);
        double at = std::stod(p[i_arr]);
        int o = dept_to_node[p[i_from]];
        int d = dept_to_node[p[i_to]];
        int u = std::stoi(p[i_urg]);
        int fid = flow_map.at({o, d});
        rows.push_back(Row{id, Arrival{at, o, d, u, sd.sp_mat[o][d], fid}});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });
    std::vector<Arrival> out;
    out.reserve(rows.size());
    for (const auto& r : rows) out.push_back(r.ar);
    return out;
}

SimData load_case_data(const std::string& json_path, const std::string& seed11_csv, int n_unseen = 50) {
    SimData sd;
    std::string js = json_util::read_all(json_path);

    sd.fixed_dir = json_util::parse_str(js, "fixedDir");
    sd.urgent_dest = (int)json_util::parse_num(js, "urgentDest");
    sd.alpha = json_util::parse_num(js, "alpha");
    double vc = json_util::parse_num(js, "vc");
    double ve = json_util::parse_num(js, "ve");
    double tl = json_util::parse_num(js, "tl");
    double tu = json_util::parse_num(js, "tu");
    double at = json_util::parse_num(js, "at");
    double p_occ = json_util::parse_num(js, "p");
    sd.t_end = json_util::parse_num(js, "T");
    sd.warm = json_util::parse_num(js, "warm");
    sd.qcap = (int)json_util::parse_num(js, "qcap");
    sd.end_qmax = (int)json_util::parse_num(js, "endQmax");
    sd.max_nodes = (int)json_util::parse_num(js, "maxNodes");
    sd.c_agv = json_util::parse_num(js, "cAGV");
    sd.c_w = json_util::parse_num(js, "cW");
    sd.c_u = json_util::parse_num(js, "cU");

    double scale = 60.0 / at;
    double v_empty = ve / scale;
    double v_loaded = vc / scale;
    sd.t_load = tl * scale;
    sd.t_unload = tu * scale;
    sd.t_inter = p_occ * scale;
    sd.lam_hr = 284.0 * sd.alpha;

    auto pd_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "pd")));
    for (int i = 0; i < 7; ++i) sd.pd_nodes[i] = (int)pd_nums[i];

    auto dept_strs = json_util::parse_all_strings(json_util::extract_bracket(js, json_util::find_key_pos(js, "depts")));
    for (int i = 0; i < 7; ++i) sd.depts[i] = dept_strs[i];

    auto flow_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "flows")));
    sd.n_flows = (int)(flow_nums.size() / 3);
    sd.flows.resize(sd.n_flows);
    for (int i = 0; i < sd.n_flows; ++i) {
        sd.flows[i] = FlowDef{(int)flow_nums[3 * i], (int)flow_nums[3 * i + 1], flow_nums[3 * i + 2]};
    }

    auto edge_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "edges")));
    int n_edges = (int)(edge_nums.size() / 3);
    std::vector<std::vector<std::pair<int, double>>> adj(sd.n_nodes);
    double edge_w[26][26] = {{0.0}};
    int deg[26] = {0};
    for (int i = 0; i < n_edges; ++i) {
        int u = (int)edge_nums[3 * i];
        int v = (int)edge_nums[3 * i + 1];
        double w = edge_nums[3 * i + 2];
        ++deg[u]; ++deg[v];
        if (sd.fixed_dir[i] == '0') {
            adj[u].push_back({v, w});
            edge_w[u][v] = w;
        } else {
            adj[v].push_back({u, w});
            edge_w[v][u] = w;
        }
    }
    for (int i = 0; i < sd.n_nodes; ++i) sd.is_inter[i] = (deg[i] >= 3) ? 1 : 0;

    // Dijkstra matching Python heapq shortest paths
    for (int start = 0; start < sd.n_nodes; ++start) {
        std::array<double, 26> dist;
        std::array<int, 26> prev;
        dist.fill(1e18);
        prev.fill(-1);
        dist[start] = 0.0;
        using P = std::pair<double, int>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0.0, start});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u]) continue;
            for (auto [v, w] : adj[u]) {
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    prev[v] = u;
                    pq.push({dist[v], v});
                }
            }
        }
        for (int end = 0; end < sd.n_nodes; ++end) {
            sd.sp_mat[start][end] = dist[end];
            std::vector<int> path;
            for (int curr = end; curr != -1; curr = prev[curr]) path.push_back(curr);
            std::reverse(path.begin(), path.end());
            for (int ld = 0; ld < 2; ++ld) {
                double spd = ld ? v_loaded : v_empty;
                std::vector<Hop> hops;
                double acc_t = 0.0;
                for (size_t i = 0; i + 1 < path.size(); ++i) {
                    int a = path[i], b = path[i + 1];
                    acc_t += edge_w[a][b] / spd;
                    if (sd.is_inter[b] || i + 2 == path.size()) {
                        hops.push_back(Hop{b, acc_t});
                        acc_t = 0.0;
                    }
                }
                sd.comp_path[start][end][ld] = std::move(hops);
            }
        }
    }

    std::map<std::pair<int, int>, int> flow_map;
    for (int i = 0; i < sd.n_flows; ++i) {
        flow_map[{sd.pd_nodes[sd.flows[i].o_dept], sd.pd_nodes[sd.flows[i].d_dept]}] = i;
    }

    auto pub_seeds = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "seeds")));
    sd.streams.push_back(load_seed11_csv(sd, seed11_csv, flow_map));
    for (size_t i = 1; i < pub_seeds.size(); ++i) {
        sd.streams.push_back(generate_arrivals(sd, (uint32_t)pub_seeds[i]));
    }
    for (int s = 101; s < 101 + n_unseen; ++s) {
        sd.streams.push_back(generate_arrivals(sd, (uint32_t)s));
    }

    sd.base_events.resize(sd.streams.size());
    for (size_t s = 0; s < sd.streams.size(); ++s) {
        sd.base_events[s].resize(sd.streams[s].size());
        for (size_t i = 0; i < sd.streams[s].size(); ++i) {
            sd.base_events[s][i] = Event{round9(sd.streams[s][i].at), 0, (int)i};
        }
    }
    return sd;
}

struct SimMetrics {
    double cost = 999999.0;
    double w_bar = 999.0;
    double u_95 = 999.0;
    int max_q = 0;
    int end_q = 0;
};

struct AgvState {
    int status = 0;
    int src = 0;
    int dst = 0;
    int hidx = 0;
    bool is_loaded = false;
    int rid = -1;
};

// Evaluates the dispatching tree priority bitmask kappa(v, r) for a candidate pair.
static inline int eval_tree_bits(const Tree& tree, const Arrival& ar, double de, double w, int qo) {
    int bits = 0;
    int shift = 12;
    int nid = 1;
    while (nid != 0) {
        const TreeNode& tn = tree.nodes[nid];
        double val;
        if (tn.feat == 0) val = ar.u;
        else if (tn.feat == 1) val = w;
        else if (tn.feat == 2) val = de;
        else if (tn.feat == 3) val = ar.dl;
        else val = qo;

        bool is_hi = val > tn.th;
        int bit = tn.is_min ? (is_hi ? 1 : 0) : (is_hi ? 0 : 1);
        bits |= (bit << shift);
        --shift;
        nid = is_hi ? tn.hi : tn.lo;
    }
    return bits;
}

SimMetrics run_sim(const SimData& sd, int stream_idx, int n_agv, const Tree& tree,
                   int q_cutoff = -1, bool brute_force = false) {
    const auto& arrivals = sd.streams[stream_idx];
    const int n_arr = (int)arrivals.size();
    int q_limit = (q_cutoff > 0) ? std::min(q_cutoff, sd.qcap) : sd.qcap;

    // Collect distinct w thresholds used in the tree for exact flow pruning even with w min!
    bool all_w_max = true;
    std::vector<double> w_min_thresh;
    if (!brute_force) {
        for (int nid = 1; nid <= tree.n_nodes; ++nid) {
            if (tree.nodes[nid].feat == 1 && tree.nodes[nid].is_min) {
                all_w_max = false;
                w_min_thresh.push_back(tree.nodes[nid].th);
            }
        }
    }

    std::vector<Event> heap_buf = sd.base_events[stream_idx];
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

    double inter_free[26] = {0.0};
    int agv_pos[32];
    AgvState agv_state[32];
    uint32_t idle_mask = (n_agv == 32) ? 0xFFFFFFFFu : ((1u << n_agv) - 1u);
    for (int i = 0; i < n_agv; ++i) agv_pos[i] = sd.pd_nodes[i % 7];

    std::vector<int> flow_q[9];
    int fq_head[9] = {0};
    int qo_map[26] = {0};
    int q_len = 0;
    int max_q = 0;
    std::vector<double> pickup_t(n_arr, -1.0);

    auto start_leg = [&](double t, int aid, int target_node, bool is_loaded, int rid) {
        int cur = agv_pos[aid];
        if (cur == target_node) {
            if (!is_loaded) {
                pickup_t[rid] = t;
                pq_push(Event{round9(t + sd.t_load), 2, aid});
                agv_state[aid].status = 2;
                agv_state[aid].rid = rid;
            } else {
                pq_push(Event{round9(t + sd.t_unload), 3, aid});
                agv_state[aid].status = 3;
                agv_state[aid].rid = rid;
            }
            return;
        }
        const auto& hops = sd.comp_path[cur][target_node][is_loaded ? 1 : 0];
        agv_state[aid].status = 1;
        agv_state[aid].src = cur;
        agv_state[aid].dst = target_node;
        agv_state[aid].hidx = 0;
        agv_state[aid].is_loaded = is_loaded;
        agv_state[aid].rid = rid;
        pq_push(Event{round9(t + hops[0].dt), 1, aid});
    };

    auto try_dispatch = [&](double t) {
        int cand_rids[305];
        while (idle_mask && q_len > 0) {
            int rep_pos[32];
            int rep_aid[32];
            int n_rep = 0;
            if (brute_force) {
                uint32_t m = idle_mask;
                while (m) {
                    int aid = __builtin_ctz(m);
                    m &= m - 1;
                    rep_pos[n_rep] = agv_pos[aid];
                    rep_aid[n_rep] = aid;
                    ++n_rep;
                }
            } else {
                uint32_t seen_pos_mask = 0;
                uint32_t m = idle_mask;
                while (m) {
                    int aid = __builtin_ctz(m);
                    m &= m - 1;
                    int p = agv_pos[aid];
                    uint32_t bit = 1u << p;
                    if (!(seen_pos_mask & bit)) {
                        seen_pos_mask |= bit;
                        rep_pos[n_rep] = p;
                        rep_aid[n_rep] = aid;
                        ++n_rep;
                    }
                }
            }

            int n_cand = 0;
            if (brute_force) {
                for (int fid = 0; fid < sd.n_flows; ++fid) {
                    const auto& fq = flow_q[fid];
                    for (int k = fq_head[fid]; k < (int)fq.size(); ++k) {
                        cand_rids[n_cand++] = fq[k];
                    }
                }
            } else if (all_w_max) {
                for (int fid = 0; fid < sd.n_flows; ++fid) {
                    if (fq_head[fid] < (int)flow_q[fid].size()) {
                        cand_rids[n_cand++] = flow_q[fid][fq_head[fid]];
                    }
                }
            } else {
                // Exact dominance with w min: in each flow (sorted descending by w),
                // only fq_head AND the first element crossing each w_min_thresh (w <= th) can ever win!
                for (int fid = 0; fid < sd.n_flows; ++fid) {
                    int h = fq_head[fid];
                    int sz = (int)flow_q[fid].size();
                    if (h >= sz) continue;
                    cand_rids[n_cand++] = flow_q[fid][h];
                    for (double th : w_min_thresh) {
                        double min_at = t - th;
                        // Find first k in [h+1, sz) with arrivals[flow_q[fid][k]].at >= min_at
                        int lo = h + 1, hi = sz;
                        while (lo < hi) {
                            int mid = (lo + hi) >> 1;
                            if (arrivals[flow_q[fid][mid]].at >= min_at) hi = mid;
                            else lo = mid + 1;
                        }
                        if (lo < sz) {
                            int rid_c = flow_q[fid][lo];
                            bool dup = false;
                            for (int x = n_cand - 1; x >= 0 && arrivals[cand_rids[x]].fid == fid; --x) {
                                if (cand_rids[x] == rid_c) { dup = true; break; }
                            }
                            if (!dup) cand_rids[n_cand++] = rid_c;
                        }
                    }
                }
            }

            int best_bits = 0x7FFFFFFF;
            double best_de = 1e18;
            int best_rid = 0x7FFFFFFF;
            int best_aid = 0x7FFFFFFF;

            for (int r_i = 0; r_i < n_rep; ++r_i) {
                int apos = rep_pos[r_i];
                int aid = rep_aid[r_i];
                const double* sp_row = sd.sp_mat[apos].data();

                for (int c_i = 0; c_i < n_cand; ++c_i) {
                    int rid = cand_rids[c_i];
                    const Arrival& ar = arrivals[rid];
                    double de = sp_row[ar.o];
                    int bits = eval_tree_bits(tree, ar, de, t - ar.at, qo_map[ar.o]);

                    bool better = (bits != best_bits) ? (bits < best_bits)
                                : (de != best_de)     ? (de < best_de)
                                : (rid != best_rid)   ? (rid < best_rid)
                                                      : (aid < best_aid);
                    if (better) {
                        best_bits = bits;
                        best_de = de;
                        best_rid = rid;
                        best_aid = aid;
                    }
                }
            }

            idle_mask &= ~(1u << best_aid);
            int fid = arrivals[best_rid].fid;
            if (best_rid == flow_q[fid][fq_head[fid]]) {
                ++fq_head[fid];
            } else {
                auto& fq = flow_q[fid];
                for (int k = fq_head[fid]; k < (int)fq.size(); ++k) {
                    if (fq[k] == best_rid) {
                        fq.erase(fq.begin() + k);
                        break;
                    }
                }
            }
            int o_node = arrivals[best_rid].o;
            --qo_map[o_node];
            --q_len;
            start_leg(t, best_aid, o_node, false, best_rid);
        }
    };

    while (!heap_buf.empty()) {
        double t = heap_buf.front().t;
        if (t > sd.t_end) break;
        bool need_dispatch = false;
        while (!heap_buf.empty() && std::abs(heap_buf.front().t - t) < 1e-9) {
            Event ev = pq_pop();
            if (ev.type == 0) {
                const Arrival& ar = arrivals[ev.idx];
                flow_q[ar.fid].push_back(ev.idx);
                ++qo_map[ar.o];
                ++q_len;
                if (q_len > max_q) max_q = q_len;
                if (max_q > q_limit) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len};
                need_dispatch = true;
            } else if (ev.type == 1) {
                int aid = ev.idx;
                AgvState& st = agv_state[aid];
                const auto& hops = sd.comp_path[st.src][st.dst][st.is_loaded ? 1 : 0];
                int node = hops[st.hidx].node;
                agv_pos[aid] = node;
                double leave_t = t;
                if (sd.is_inter[node]) {
                    leave_t = std::max(t, inter_free[node]) + sd.t_inter;
                    inter_free[node] = leave_t;
                }
                if (st.hidx + 1 < (int)hops.size()) {
                    ++st.hidx;
                    pq_push(Event{round9(leave_t + hops[st.hidx].dt), 1, aid});
                } else {
                    if (!st.is_loaded) {
                        pickup_t[st.rid] = leave_t;
                        pq_push(Event{round9(leave_t + sd.t_load), 2, aid});
                        st.status = 2;
                    } else {
                        pq_push(Event{round9(leave_t + sd.t_unload), 3, aid});
                        st.status = 3;
                    }
                }
            } else if (ev.type == 2) {
                int aid = ev.idx;
                int rid = agv_state[aid].rid;
                start_leg(t, aid, arrivals[rid].d, true, rid);
            } else {
                int aid = ev.idx;
                agv_state[aid].status = 0;
                idle_mask |= (1u << aid);
                need_dispatch = true;
            }
        }
        if (need_dispatch) try_dispatch(t);
    }

    if (q_len > sd.end_qmax) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len};

    double sum_w = 0.0;
    int cnt_all = 0;
    std::vector<double> waits_urg;
    waits_urg.reserve(2000);
    for (int rid = 0; rid < n_arr; ++rid) {
        const Arrival& ar = arrivals[rid];
        if (ar.at >= sd.warm && pickup_t[rid] >= 0.0) {
            double wt = pickup_t[rid] - ar.at;
            sum_w += wt;
            ++cnt_all;
            if (ar.u == 1) waits_urg.push_back(wt);
        }
    }
    if (cnt_all == 0 || waits_urg.empty()) return SimMetrics{999999.0, 999.0, 999.0, max_q, q_len};

    double w_bar = sum_w / cnt_all;
    std::sort(waits_urg.begin(), waits_urg.end());
    double pos = 0.95 * (waits_urg.size() - 1);
    size_t lo = (size_t)std::floor(pos);
    size_t hi = (size_t)std::ceil(pos);
    double u95 = waits_urg[lo] + (pos - lo) * (waits_urg[hi] - waits_urg[lo]);
    double cost = sd.c_agv * n_agv + sd.c_w * sd.lam_hr * w_bar + sd.c_u * u95;
    return SimMetrics{cost, w_bar, u95, max_q, q_len};
}

bool is_valid_tree(const Tree& t) {
    if (t.n_nodes < 1 || t.n_nodes > 9) return false;
    bool visited[10] = {false};
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
    for (int i = 1; i <= t.n_nodes; ++i) if (!visited[i]) return false;
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

std::pair<int, Tree> load_csv_tree(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    int n_agv = 19, max_id = 0;
    Tree t;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> parts;
        while (std::getline(ss, tok, ',')) parts.push_back(tok);
        if (parts.empty()) continue;
        if (parts[0] == "fleet") n_agv = std::stoi(parts[1]);
        else if (parts[0] == "key") {
            int nid = std::stoi(parts[1]);
            max_id = std::max(max_id, nid);
            t.nodes[nid].feat = feat_from_str(parts[2]);
            t.nodes[nid].is_min = (parts[3] == "min");
            t.nodes[nid].th = std::stod(parts[4]);
            t.nodes[nid].lo = std::stoi(parts[5]);
            t.nodes[nid].hi = std::stoi(parts[6]);
        }
    }
    t.n_nodes = max_id;
    return {n_agv, canonicalize_tree(t)};
}

void save_csv_tree(const SimData& sd, const std::string& path, int n_agv, const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ofstream out(path);
    out << "dir," << sd.fixed_dir << "\n";
    out << "fleet," << n_agv << "\n";
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        out << "key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ",";
        if (tn.feat == 0) out << "0.5";
        else if (std::abs(tn.th - std::round(tn.th)) < 1e-9) out << (long long)std::llround(tn.th);
        else out << tn.th;
        out << "," << tn.lo << "," << tn.hi << "\n";
    }
}

double eval_streams(const SimData& sd, int n_agv, const Tree& t, int start_s, int end_s, int q_cut = -1, double* out_max = nullptr) {
    double sum = 0.0, mx = 0.0;
    int cnt = end_s - start_s;
    for (int s = start_s; s < end_s; ++s) {
        double c = run_sim(sd, s, n_agv, t, q_cut).cost;
        if (c >= 900000.0) { if (out_max) *out_max = 999999.0; return 999999.0; }
        sum += c;
        mx = std::max(mx, c);
    }
    if (out_max) *out_max = mx;
    return sum / cnt;
}

std::string tree_key_str(const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ostringstream os;
    for (int i = 1; i <= t.n_nodes; ++i) {
        const auto& n = t.nodes[i];
        os << n.feat << (n.is_min ? 'm' : 'M') << std::llround(n.th * 100) << ':' << n.lo << ':' << n.hi << ';';
    }
    return os.str();
}

struct Split {
    int feat;
    bool is_min;
    double th;
};

static const std::vector<double> DE_GRID = {0, 25, 65, 90, 115, 140, 155, 180};
static const std::vector<double> DL_GRID = {25, 50, 65, 90};
static const std::vector<double> QO_GRID = {1, 2, 3, 4, 5};
static const std::vector<double> W_GRID  = {
    0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0, 7.0, 8.0
};

// Executes fn(i) for i in [0, count) using up to n_threads worker threads.
template <typename Fn>
void parallel_for(int count, int n_threads, Fn&& fn) {
    int nt = std::max(1, std::min(n_threads, count));
    if (nt <= 1) {
        for (int i = 0; i < count; ++i) fn(i);
        return;
    }
    std::vector<std::thread> ths;
    ths.reserve(nt);
    for (int ti = 0; ti < nt; ++ti) {
        ths.emplace_back([&, ti]() {
            for (int i = ti; i < count; i += nt) fn(i);
        });
    }
    for (auto& th : ths) th.join();
}

// Evaluates weighted multi-seed objective: 0.15 * Seed11 + 0.85 * mean(unseen[5..5+n_tail)).
double eval_w51(const SimData& sd, int n_agv, const Tree& t, int n_tail = 50,
                int q_cut = -1, double* out_s11 = nullptr, double* out_tail = nullptr) {
    double s11 = run_sim(sd, 0, n_agv, t, q_cut).cost;
    if (s11 >= 900000.0) return 999999.0;
    double tail = eval_streams(sd, n_agv, t, 5, 5 + n_tail, q_cut);
    if (tail >= 900000.0) return 999999.0;
    if (out_s11) *out_s11 = s11;
    if (out_tail) *out_tail = tail;
    return 0.15 * s11 + 0.85 * tail;
}

std::vector<double> eval_batch_w51(const SimData& sd, int n_agv, const std::vector<Tree>& trees,
                                   int n_threads, int n_tail = 50, int q_cut = -1) {
    std::vector<double> scores(trees.size(), 999999.0);
    parallel_for((int)trees.size(), n_threads, [&](int i) {
        scores[i] = eval_w51(sd, n_agv, trees[i], n_tail, q_cut);
    });
    return scores;
}

std::vector<SimMetrics> eval_stream_range(const SimData& sd, int n_agv, const Tree& t,
                                          int start_s, int count, int n_threads) {
    std::vector<SimMetrics> out(count);
    parallel_for(count, n_threads, [&](int i) {
        out[i] = run_sim(sd, start_s + i, n_agv, t);
    });
    return out;
}

struct RangeStats {
    double mean_cost = 999999.0;
    double std_cost = 0.0;
    double mean_w = 0.0;
    double mean_u95 = 0.0;
    double max_cost = 999999.0;
    int max_q = 0;
    int max_end_q = 0;
    int disq = 0;
    std::vector<double> costs;
};

RangeStats summarize_range(const SimData& sd, int n_agv, const Tree& t, int start_s, int count, int n_threads) {
    auto ms = eval_stream_range(sd, n_agv, t, start_s, count, n_threads);
    RangeStats st;
    st.costs.reserve(count);
    double sc = 0.0, sw = 0.0, su = 0.0;
    for (const auto& m : ms) {
        st.max_q = std::max(st.max_q, m.max_q);
        st.max_end_q = std::max(st.max_end_q, m.end_q);
        if (m.cost >= 900000.0) {
            ++st.disq;
        } else {
            sc += m.cost;
            sw += m.w_bar;
            su += m.u_95;
            st.costs.push_back(m.cost);
        }
    }
    int ok = (int)st.costs.size();
    if (ok == 0) return st;
    st.mean_cost = sc / ok;
    st.mean_w = sw / ok;
    st.mean_u95 = su / ok;
    double var = 0.0;
    double mx = 0.0;
    for (double c : st.costs) {
        var += (c - st.mean_cost) * (c - st.mean_cost);
        mx = std::max(mx, c);
    }
    st.std_cost = ok > 1 ? std::sqrt(var / (ok - 1)) : 0.0;
    st.max_cost = mx;
    return st;
}

std::vector<int> sorted_order(const std::vector<double>& vals) {
    std::vector<int> idx(vals.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return vals[a] < vals[b]; });
    return idx;
}

std::vector<double> threshold_grid_for(int feat, double current_th) {
    if (feat == 2) return DE_GRID;
    if (feat == 3) return DL_GRID;
    if (feat == 4) return QO_GRID;
    std::vector<double> grid = W_GRID;
    for (int d = -4; d <= 4; ++d) {
        grid.push_back(std::round(std::max(0.1, current_th + d * 0.1) * 100.0) / 100.0);
    }
    std::sort(grid.begin(), grid.end());
    grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
    return grid;
}

std::vector<Tree> generate_neighbor_trees(const Tree& base_t) {
    std::vector<Tree> cands;
    std::set<std::string> seen;
    seen.insert(tree_key_str(base_t));

    auto try_add = [&](const Tree& cand) {
        if (is_valid_tree(cand) && seen.insert(tree_key_str(cand)).second) {
            cands.push_back(cand);
        }
    };

    for (int nid = 1; nid <= base_t.n_nodes; ++nid) {
        const auto& tn = base_t.nodes[nid];
        if (tn.feat == 0) {
            Tree cand = base_t;
            cand.nodes[nid].is_min = !tn.is_min;
            try_add(cand);
        } else {
            auto grid = threshold_grid_for(tn.feat, tn.th);
            for (bool cm : {true, false}) {
                for (double cth : grid) {
                    Tree cand = base_t;
                    cand.nodes[nid].is_min = cm;
                    cand.nodes[nid].th = cth;
                    try_add(cand);
                }
            }
        }
        if (tn.lo == 0 && tn.hi == 0 && nid != 1) {
            Tree cand = base_t;
            for (int pid = 1; pid <= cand.n_nodes; ++pid) {
                if (cand.nodes[pid].lo == nid) cand.nodes[pid].lo = 0;
                if (cand.nodes[pid].hi == nid) cand.nodes[pid].hi = 0;
            }
            try_add(canonicalize_tree(cand));
        }
    }
    return cands;
}

// Fast 2-stage robust coordinate polish: screens all single-node changes on 12 streams
// (Seed11 + unseen 101..111 with q_cut=65), then verifies top 10 on full w51 (51 streams).
Tree coordinate_polish(const SimData& sd, int n_agv, Tree base_t, double& out_cost, int n_threads) {
    Tree best_t = canonicalize_tree(base_t);
    double best_c = eval_w51(sd, n_agv, best_t);

    bool improved = true;
    while (improved) {
        improved = false;
        auto cands = generate_neighbor_trees(best_t);
        if (cands.empty()) break;

        auto scr = eval_batch_w51(sd, n_agv, cands, n_threads, 11, 65);
        auto idx = sorted_order(scr);

        std::vector<Tree> top_trees;
        for (int k = 0; k < std::min((int)idx.size(), 10); ++k) {
            if (scr[idx[k]] < 900000.0) top_trees.push_back(cands[idx[k]]);
        }
        auto exact_c = eval_batch_w51(sd, n_agv, top_trees, n_threads);
        for (size_t k = 0; k < top_trees.size(); ++k) {
            if (exact_c[k] < best_c - 1e-4) {
                best_c = exact_c[k];
                best_t = top_trees[k];
                improved = true;
            }
        }
    }
    out_cost = best_c;
    return best_t;
}

void run_analysis(const SimData& sd, const std::string& csv_path, int n_threads) {
    auto [base_n, base_t] = load_csv_tree(csv_path);
    int n_unseen = std::min(50, (int)sd.streams.size() - 5);
    std::cout << "=== FULL ANALYSIS FOR " << csv_path << " (n=" << base_n << ", nodes=" << base_t.n_nodes << ") ===\n";

    std::cout << "\n[1] Public 5-Seed Breakdown (Seeds 11..15):\n";
    double p_sum = 0, p_w = 0, p_u = 0;
    int p_maxq = 0, p_endq = 0;
    const int pub_seeds[5] = {11, 12, 13, 14, 15};
    for (int s = 0; s < 5; ++s) {
        SimMetrics m = run_sim(sd, s, base_n, base_t);
        p_sum += m.cost; p_w += m.w_bar; p_u += m.u_95;
        p_maxq = std::max(p_maxq, m.max_q); p_endq = std::max(p_endq, m.end_q);
        std::cout << "  Seed " << pub_seeds[s] << ": cost=" << std::fixed << std::setprecision(2) << m.cost
                  << " | w_bar=" << std::setprecision(4) << m.w_bar << " | u95=" << m.u_95
                  << " | maxQ=" << m.max_q << " | endQ=" << m.end_q << "\n";
    }
    std::cout << "  MEAN   : cost=" << std::setprecision(2) << p_sum / 5.0
              << " | agv=" << 15.0 * base_n << " | wait_cost=" << 102.24 * (p_w / 5.0)
              << " | urg_cost=" << 100.0 * (p_u / 5.0) << " | w_bar=" << std::setprecision(4) << p_w / 5.0
              << " | u95=" << p_u / 5.0 << " | maxQ=" << p_maxq << " | endQ=" << p_endq << "\n";

    std::cout << "\n[2] Fleet Size Sensitivity (n=16..23) on Public (5) & Unseen (50):\n";
    for (int na = 16; na <= 23; ++na) {
        double pm_c = 0, pm_w = 0, pm_u = 0;
        int pm_disq = 0, pm_mq = 0;
        for (int s = 0; s < 5; ++s) {
            SimMetrics m = run_sim(sd, s, na, base_t);
            if (m.cost >= 900000.0) ++pm_disq;
            else { pm_c += m.cost; pm_w += m.w_bar; pm_u += m.u_95; }
            pm_mq = std::max(pm_mq, m.max_q);
        }
        RangeStats st = summarize_range(sd, na, base_t, 5, n_unseen, n_threads);
        double ustd = st.costs.size() > 1 ? st.std_cost * std::sqrt((double)(st.costs.size() - 1) / st.costs.size()) : 0.0;
        bool ok_u = !st.costs.empty();
        std::cout << "  n=" << std::setw(2) << na << " | pub=" << std::setprecision(2) << (pm_disq ? 999999.0 : pm_c / 5.0)
                  << " (w=" << std::setprecision(4) << (pm_disq ? 999.0 : pm_w / 5.0) << ", u95=" << (pm_disq ? 999.0 : pm_u / 5.0)
                  << ", maxQ=" << pm_mq << ") | unseen50: mean=" << std::setprecision(2) << st.mean_cost
                  << " (w=" << std::setprecision(4) << (ok_u ? st.mean_w : 999.0)
                  << ", u95=" << (ok_u ? st.mean_u95 : 999.0) << ") std=" << std::setprecision(2) << ustd
                  << " max=" << st.max_cost << " maxQ=" << st.max_q << " endQ=" << st.max_end_q
                  << " disq=" << st.disq << "/" << n_unseen << "\n";
    }

    auto summarize_unseen = [&](const Tree& tr, int& out_disq) {
        RangeStats st = summarize_range(sd, base_n, tr, 5, n_unseen, n_threads);
        out_disq = st.disq;
        return st.mean_cost;
    };

    std::cout << "\n[3] Node Ablation Analysis (Removing each node 2..N):\n";
    double full_pub = p_sum / 5.0;
    int dummy_dq = 0;
    double full_u_mean = summarize_unseen(base_t, dummy_dq);
    for (int rem = 2; rem <= base_t.n_nodes; ++rem) {
        Tree ab = base_t;
        for (int pid = 1; pid <= ab.n_nodes; ++pid) {
            if (ab.nodes[pid].lo == rem) ab.nodes[pid].lo = 0;
            if (ab.nodes[pid].hi == rem) ab.nodes[pid].hi = 0;
        }
        ab = canonicalize_tree(ab);
        double ac = eval_streams(sd, base_n, ab, 0, 5);
        int au_disq = 0;
        double au_mean = summarize_unseen(ab, au_disq);
        const auto& rn = base_t.nodes[rem];
        std::cout << "  Drop #" << rem << " (" << FEAT_NAMES[rn.feat] << " " << (rn.is_min ? "min" : "max") << " " << rn.th
                  << "): pub=" << std::setprecision(2) << ac << " (delta=" << std::showpos << (ac - full_pub) << std::noshowpos
                  << ") | unseen50=" << au_mean << " (delta=" << std::showpos << (au_mean - full_u_mean) << std::noshowpos << ", disq=" << au_disq << ")\n";
    }

    std::cout << "\n[4] Threshold Sensitivity on Unseen 50 Seeds Around Optimum:\n";
    for (int nid = 1; nid <= base_t.n_nodes; ++nid) {
        const auto& tn = base_t.nodes[nid];
        if (tn.feat == 0) continue;
        std::vector<double> vals;
        if (tn.feat == 1) vals = {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
        else if (tn.feat == 2) vals = {0, 25, 65, 90, 115};
        else if (tn.feat == 3) vals = {25, 50, 65, 90};
        else vals = {1, 2, 3, 4};
        if (std::find(vals.begin(), vals.end(), tn.th) == vals.end()) {
            vals.push_back(tn.th);
            std::sort(vals.begin(), vals.end());
        }
        std::cout << "  Node #" << nid << " (" << FEAT_NAMES[tn.feat] << " " << (tn.is_min ? "min" : "max") << ", opt=" << tn.th << "):\n";
        for (double v : vals) {
            Tree cand = base_t;
            cand.nodes[nid].th = v;
            int dq = 0;
            double um = summarize_unseen(cand, dq);
            std::cout << "    th=" << std::setw(5) << v << " -> unseen50=" << std::setprecision(2) << um << " (disq=" << dq << ")\n";
        }
    }
}

struct AstNode { int feat; bool is_min; double th; int lo; int hi; };
using Ast = std::vector<AstNode>;

std::pair<Ast, int> tree_to_ast(const Tree& t, std::vector<int>* tid_to_ast = nullptr) {
    Ast a;
    if (tid_to_ast) tid_to_ast->assign(t.n_nodes + 1, -1);
    auto build = [&](auto& self, int nid) -> int {
        if (nid == 0) return -1;
        int l = self(self, t.nodes[nid].lo);
        int r = self(self, t.nodes[nid].hi);
        int idx = (int)a.size();
        a.push_back({t.nodes[nid].feat, t.nodes[nid].is_min, t.nodes[nid].th, l, r});
        if (tid_to_ast) (*tid_to_ast)[nid] = idx;
        return idx;
    };
    int root = build(build, 1);
    return {a, root};
}

// Reconstructs a bounded valid <=9-node Tree from an AST via BFS, omitting duplicate path features.
Tree ast_to_valid_tree(const Ast& a, int root_idx) {
    Tree out;
    out.n_nodes = 0;
    if (root_idx < 0) return out;
    struct Item { int ast_idx; int parent_tid; bool is_hi; int feat_mask; };
    std::queue<Item> q;
    q.push({root_idx, 0, false, 0});
    while (!q.empty() && out.n_nodes < 9) {
        Item cur = q.front();
        q.pop();
        if (cur.ast_idx < 0 || cur.ast_idx >= (int)a.size()) continue;
        const AstNode& an = a[cur.ast_idx];
        if (cur.feat_mask & (1 << an.feat)) continue;
        int tid = ++out.n_nodes;
        out.nodes[tid] = {an.feat, an.is_min, an.th, 0, 0};
        if (cur.parent_tid != 0) {
            if (cur.is_hi) out.nodes[cur.parent_tid].hi = tid;
            else out.nodes[cur.parent_tid].lo = tid;
        }
        int nm = cur.feat_mask | (1 << an.feat);
        if (an.lo >= 0) q.push({an.lo, tid, false, nm});
        if (an.hi >= 0) q.push({an.hi, tid, true, nm});
    }
    return canonicalize_tree(out);
}

// Wraps an existing AST node under a newly created split node (edge-split).
void insert_split_above(Ast& ast, int& root, int target_idx, const Split& sp, bool old_on_hi) {
    int new_idx = (int)ast.size();
    ast.push_back({sp.feat, sp.is_min, sp.th, old_on_hi ? -1 : target_idx, old_on_hi ? target_idx : -1});
    if (target_idx == root) {
        root = new_idx;
        return;
    }
    for (int i = 0; i < new_idx; ++i) {
        if (ast[i].lo == target_idx) ast[i].lo = new_idx;
        if (ast[i].hi == target_idx) ast[i].hi = new_idx;
    }
}

std::vector<Tree> build_reconstruct_seeds(const std::string& seed_csv, const std::vector<Split>& splits) {
    std::vector<Tree> pool;
    std::set<std::string> seen;
    auto push_tree = [&](const Tree& tr) {
        if (!is_valid_tree(tr)) return;
        Tree ct = canonicalize_tree(tr);
        if (seen.insert(tree_key_str(ct)).second) pool.push_back(ct);
    };

    std::vector<std::string> candidate_csvs = {"answer_final_best.csv", "reconstruct_best_19.csv", "robust_best_19.csv", "answer (1).csv"};
    if (!seed_csv.empty()) candidate_csvs.insert(candidate_csvs.begin(), seed_csv);
    for (const auto& fn : candidate_csvs) {
        std::ifstream chk(fn);
        if (chk.good()) push_tree(load_csv_tree(fn).second);
    }

    const std::vector<Split> top_roots = {
        {4, false, 1}, {4, false, 2}, {4, true, 1},
        {3, true, 65}, {3, true, 25}, {3, false, 65},
        {1, false, 2.0}, {1, false, 4.0}, {1, true, 1.0}, {1, true, 2.5},
        {0, false, 0.5}
    };
    const std::vector<double> de_vals = {0, 25, 65, 90, 115};
    for (const auto& r1 : top_roots) {
        for (double d_lo : de_vals) {
            for (double d_hi : de_vals) {
                Tree t3; t3.n_nodes = 3;
                t3.nodes[1] = {r1.feat, r1.is_min, r1.th, 2, 3};
                t3.nodes[2] = {2, true, d_lo, 0, 0};
                t3.nodes[3] = {2, true, d_hi, 0, 0};
                push_tree(t3);
                if (r1.feat != 0 && r1.feat != 4) {
                    Tree t7 = t3; t7.n_nodes = 7;
                    t7.nodes[2].lo = 4; t7.nodes[2].hi = 5;
                    t7.nodes[3].lo = 6; t7.nodes[3].hi = 7;
                    t7.nodes[4] = {4, false, 1, 0, 0};
                    t7.nodes[5] = {0, false, 0.5, 0, 0};
                    t7.nodes[6] = {0, false, 0.5, 0, 0};
                    t7.nodes[7] = {1, true, 1.0, 0, 0};
                    push_tree(t7);
                }
            }
        }
    }

    if (!pool.empty()) {
        Tree base = pool[0];
        for (int target = 1; target <= base.n_nodes; ++target) {
            for (const auto& sp : splits) {
                for (bool old_on_hi : {false, true}) {
                    std::vector<int> tid_to_ast;
                    auto [ast, root] = tree_to_ast(base, &tid_to_ast);
                    insert_split_above(ast, root, tid_to_ast[target], sp, old_on_hi);
                    push_tree(ast_to_valid_tree(ast, root));
                }
            }
        }
    }
    return pool;
}

Tree mutate_reconstruct_child(const Tree& p1, const Tree& p2, const std::vector<Split>& splits, std::mt19937& rng) {
    auto [ast, root] = tree_to_ast(p1);
    int op = rng() % 5;

    if (op == 0) {
        auto [ast2, root2] = tree_to_ast(p2);
        (void)root2;
        int donor_idx = rng() % ast2.size();
        int base_sz = (int)ast.size();
        for (const auto& dn : ast2) {
            ast.push_back({dn.feat, dn.is_min, dn.th, dn.lo >= 0 ? dn.lo + base_sz : -1, dn.hi >= 0 ? dn.hi + base_sz : -1});
        }
        int target = rng() % base_sz;
        if (rng() % 2) ast[target].lo = donor_idx + base_sz;
        else ast[target].hi = donor_idx + base_sz;
    } else if (op == 1) {
        int target = rng() % ast.size();
        const auto& sp = splits[rng() % splits.size()];
        insert_split_above(ast, root, target, sp, (rng() % 2) == 1);
    } else if (op == 2 && ast.size() > 2) {
        int target = rng() % ast.size();
        int promo = (rng() % 2) ? ast[target].lo : ast[target].hi;
        if (promo >= 0) {
            if (target == root) root = promo;
            else {
                for (auto& node : ast) {
                    if (node.lo == target) node.lo = promo;
                    if (node.hi == target) node.hi = promo;
                }
            }
        }
    } else if (op == 3) {
        int target = rng() % ast.size();
        std::swap(ast[target].lo, ast[target].hi);
    } else {
        int nm = 1 + (rng() % 2);
        for (int m = 0; m < nm; ++m) {
            int target = rng() % ast.size();
            const auto& sp = splits[rng() % splits.size()];
            if ((rng() % 3) == 0 && ast.size() < 9) {
                int new_idx = (int)ast.size();
                ast.push_back({sp.feat, sp.is_min, sp.th, -1, -1});
                if (ast[target].lo < 0) ast[target].lo = new_idx;
                else if (ast[target].hi < 0) ast[target].hi = new_idx;
            } else {
                ast[target].feat = sp.feat;
                ast[target].is_min = sp.is_min;
                ast[target].th = sp.th;
            }
        }
    }
    return ast_to_valid_tree(ast, root);
}

int run_reconstruct(const SimData& sd, const std::string& seed_csv, int n_agv, int n_threads) {
    auto t0 = std::chrono::steady_clock::now();
    const std::vector<Split> splits = {
        {0, false, 0.5}, {0, true, 0.5},
        {2, true, 0}, {2, true, 25}, {2, true, 65}, {2, true, 90}, {2, true, 115},
        {1, false, 1.0}, {1, false, 2.0}, {1, false, 3.5}, {1, false, 5.5},
        {1, true, 0.5}, {1, true, 0.85}, {1, true, 1.5}, {1, true, 2.5}, {1, true, 4.0},
        {4, false, 1}, {4, false, 2}, {4, true, 1},
        {3, true, 25}, {3, true, 50}, {3, true, 65}, {3, false, 65}
    };

    auto pop_trees = build_reconstruct_seeds(seed_csv, splits);
    std::cout << "[Reconstruct Init] Screening " << pop_trees.size() << " multi-tier + edge-split seeds...\n" << std::flush;
    auto iscr = eval_batch_w51(sd, n_agv, pop_trees, n_threads, 11, 65);
    auto iidx = sorted_order(iscr);

    std::vector<Tree> init_eval;
    std::map<int, int> fcnt;
    std::set<std::string> ieval_seen;
    for (int idx : iidx) {
        if (iscr[idx] >= 900000.0) break;
        const Tree& tr = pop_trees[idx];
        int f = tr.nodes[1].feat;
        if ((int)init_eval.size() < 100 || fcnt[f] < 10) {
            if (ieval_seen.insert(tree_key_str(tr)).second) {
                init_eval.push_back(tr);
                ++fcnt[f];
            }
        }
        if ((int)init_eval.size() >= 140) break;
    }

    auto init_c = eval_batch_w51(sd, n_agv, init_eval, n_threads);
    struct GPInd { double c; Tree t; };
    std::vector<GPInd> pop;
    for (size_t i = 0; i < init_eval.size(); ++i) {
        if (init_c[i] < 900000.0) pop.push_back({init_c[i], init_eval[i]});
    }
    std::sort(pop.begin(), pop.end(), [](const GPInd& a, const GPInd& b) { return a.c < b.c; });
    std::cout << "[Reconstruct Gen 0] pop=" << pop.size() << " | best w51=" << std::fixed << std::setprecision(2) << pop[0].c << "\n" << std::flush;

    std::mt19937 rng(202619);
    for (int gen = 1; gen <= 12; ++gen) {
        std::vector<Tree> kids;
        std::set<std::string> kseen;
        for (const auto& ind : pop) kseen.insert(tree_key_str(ind.t));

        auto tournament = [&]() -> const Tree& {
            int best_i = rng() % pop.size();
            for (int k = 0; k < 3; ++k) {
                int j = rng() % pop.size();
                if (pop[j].c < pop[best_i].c) best_i = j;
            }
            return pop[best_i].t;
        };

        int attempts = 0;
        while ((int)kids.size() < 320 && ++attempts < 4000) {
            Tree child = mutate_reconstruct_child(tournament(), tournament(), splits, rng);
            if (is_valid_tree(child) && kseen.insert(tree_key_str(child)).second) {
                kids.push_back(child);
            }
        }

        auto kscr = eval_batch_w51(sd, n_agv, kids, n_threads, 11, 65);
        auto kidx = sorted_order(kscr);

        std::vector<Tree> eval_kids;
        for (int k = 0; k < std::min((int)kidx.size(), 60); ++k) {
            if (kscr[kidx[k]] < 900000.0) eval_kids.push_back(kids[kidx[k]]);
        }
        auto ek_c = eval_batch_w51(sd, n_agv, eval_kids, n_threads);
        std::vector<GPInd> new_inds;
        for (size_t k = 0; k < eval_kids.size(); ++k) {
            if (ek_c[k] < 900000.0) new_inds.push_back({ek_c[k], eval_kids[k]});
        }
        std::sort(new_inds.begin(), new_inds.end(), [](const GPInd& a, const GPInd& b) { return a.c < b.c; });

        for (int k = 0; k < std::min((int)new_inds.size(), 3); ++k) {
            double pc = 0;
            Tree pt = coordinate_polish(sd, n_agv, new_inds[k].t, pc, n_threads);
            pop.push_back({pc, pt});
        }
        for (const auto& ni : new_inds) pop.push_back(ni);

        std::sort(pop.begin(), pop.end(), [](const GPInd& a, const GPInd& b) { return a.c < b.c; });
        std::vector<GPInd> next_pop;
        std::set<std::string> dedup;
        for (const auto& ind : pop) {
            if (dedup.insert(tree_key_str(ind.t)).second) next_pop.push_back(ind);
            if ((int)next_pop.size() >= 80) break;
        }
        pop = std::move(next_pop);
        std::cout << "  [Reconstruct Gen " << std::setw(2) << gen << "] best w51=" << std::fixed << std::setprecision(2) << pop[0].c
                  << " (nodes=" << pop[0].t.n_nodes << ", root=" << FEAT_NAMES[pop[0].t.nodes[1].feat] << ")\n" << std::flush;
    }

    Tree best_t = pop[0].t;
    double best_c = pop[0].c;
    for (int k = 0; k < std::min((int)pop.size(), 5); ++k) {
        double pc = 0;
        Tree pt = coordinate_polish(sd, n_agv, pop[k].t, pc, n_threads);
        if (pc < best_c) { best_c = pc; best_t = pt; }
    }

    double s11 = 0, u50 = 0;
    eval_w51(sd, n_agv, best_t, 50, -1, &s11, &u50);
    double pub5 = eval_streams(sd, n_agv, best_t, 0, 5);
    RangeStats hst = summarize_range(sd, n_agv, best_t, 55, 50, n_threads);

    save_csv_tree(sd, "reconstruct_best_19.csv", n_agv, best_t);
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "\n[Reconstruct Final] seed11=" << s11 << " | pub5=" << pub5
              << " | u50(101..150)=" << u50 << " | w51=" << best_c
              << " | HELD-OUT 50(151..200)=" << hst.mean_cost << " (disq=" << hst.disq << "/50)"
              << " | time=" << std::chrono::duration<double>(t1 - t0).count() << "s\n" << std::flush;
    return 0;
}

void run_eval_cmd(const SimData& sd, const std::string& csv_path, const std::string& mode, int n_threads) {
    auto [n_agv, tree] = load_csv_tree(csv_path);
    if (mode == "--eval") {
        double sum = 0.0;
        std::cout << "[" << csv_path << "] n=" << n_agv << ", nodes=" << tree.n_nodes << " | 5-seed:";
        for (int s = 0; s < 5; ++s) {
            double c = run_sim(sd, s, n_agv, tree).cost;
            sum += c;
            std::cout << " " << std::fixed << std::setprecision(4) << c;
        }
        std::cout << " | mean=" << std::setprecision(4) << (sum / 5.0) << "\n";
        return;
    }

    if (mode == "--eval100") {
        double s11 = run_sim(sd, 0, n_agv, tree).cost;
        double pub5 = eval_streams(sd, n_agv, tree, 0, 5);
        RangeStats a = summarize_range(sd, n_agv, tree, 5, 50, n_threads);
        RangeStats b = summarize_range(sd, n_agv, tree, 55, 50, n_threads);
        double u100 = (a.mean_cost * (50 - a.disq) + b.mean_cost * (50 - b.disq)) / std::max(1, 100 - a.disq - b.disq);
        std::cout << "[" << csv_path << "] n=" << n_agv << " | seed11=" << std::fixed << std::setprecision(2) << s11
                  << " | pub5=" << pub5 << " | u50(101..150)=" << a.mean_cost << " (dq=" << a.disq
                  << ") | held50(151..200)=" << b.mean_cost << " (dq=" << b.disq << ") | all100=" << u100 << "\n";
        return;
    }

    if (mode == "--eval300") {
        RangeStats st = summarize_range(sd, n_agv, tree, 105, 300, n_threads);
        std::cout << "[" << csv_path << "] n=" << n_agv
                  << " | HELD300(201..500)=" << std::fixed << std::setprecision(2) << st.mean_cost << " +/- " << st.std_cost
                  << " | w_bar=" << std::setprecision(4) << st.mean_w << " | u95=" << st.mean_u95
                  << " | maxQ=" << st.max_q << " | endQ=" << st.max_end_q << " | dq=" << st.disq << "/300\n";
        return;
    }

    // --eval800: HELD300 (201..500) + NEW500 (501..1000)
    RangeStats h3 = summarize_range(sd, n_agv, tree, 105, 300, n_threads);
    RangeStats h5 = summarize_range(sd, n_agv, tree, 405, 500, n_threads);
    RangeStats all = summarize_range(sd, n_agv, tree, 105, 800, n_threads);
    int b_1185 = 0, n_blocks = (int)all.costs.size() / 5;
    for (int b = 0; b < n_blocks; ++b) {
        double m = 0.0;
        for (int k = 0; k < 5; ++k) m += all.costs[b * 5 + k];
        if (m / 5.0 < 1185.42) ++b_1185;
    }
    std::cout << "[" << csv_path << "] n=" << n_agv
              << " | HELD300=" << std::fixed << std::setprecision(2) << h3.mean_cost
              << " | NEW500=" << h5.mean_cost
              << " | ALL800=" << all.mean_cost << " +/- " << all.std_cost
              << " (w=" << std::setprecision(4) << all.mean_w << ", u95=" << all.mean_u95 << ")"
              << " | P5(<1185.42)=" << std::setprecision(1) << (n_blocks ? 100.0 * b_1185 / n_blocks : 0.0) << "%"
              << " | maxQ=" << all.max_q << " | endQ=" << all.max_end_q << " | dq=" << all.disq << "/800\n";
}

int run_verify_cmd(const SimData& sd, const std::string& csv_path) {
    std::ifstream in(csv_path);
    std::string line, dir_str;
    while (std::getline(in, line)) {
        if (line.rfind("dir,", 0) == 0) {
            dir_str = line.substr(4);
            while (!dir_str.empty() && (dir_str.back() == '\r' || dir_str.back() == ' ')) dir_str.pop_back();
            break;
        }
    }
    auto [n_agv, tree] = load_csv_tree(csv_path);
    bool dir_ok = (dir_str == sd.fixed_dir);
    bool fleet_ok = (n_agv >= 1 && n_agv <= 30);
    bool tree_ok = is_valid_tree(tree);
    double max_diff = 0.0;
    for (int s = 0; s < 15; ++s) {
        double cf = run_sim(sd, s, n_agv, tree, -1, false).cost;
        double cb = run_sim(sd, s, n_agv, tree, -1, true).cost;
        max_diff = std::max(max_diff, std::abs(cf - cb));
    }
    bool parity_ok = (max_diff <= 1e-9);
    std::cout << "[" << csv_path << "] dir=" << (dir_ok ? "OK" : "FAIL")
              << " | fleet=" << n_agv << (fleet_ok ? "(OK)" : "(FAIL)")
              << " | tree=" << tree.n_nodes << "nodes" << (tree_ok ? "(OK)" : "(FAIL)")
              << " | max |fast - brute| across 15 streams = " << std::fixed << std::setprecision(9) << max_diff
              << (parity_ok ? " (PASS)" : " (FAIL)") << "\n";
    return (dir_ok && fleet_ok && tree_ok && parity_ok) ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string mode = (argc >= 2) ? argv[1] : "";
    int n_unseen = 50;
    if (mode == "--eval100" || mode == "--reconstruct") n_unseen = 100;
    else if (mode == "--eval300") n_unseen = 400;
    else if (mode == "--eval800") n_unseen = 900;

    SimData sd = load_case_data("agv_case_data (1).json", "agv_dispatch_log_seed11 (1).csv", n_unseen);
    int n_threads = std::max(1, (int)std::thread::hardware_concurrency());

    if (argc >= 3 && (mode == "--eval" || mode == "--eval100" || mode == "--eval300" || mode == "--eval800")) {
        run_eval_cmd(sd, argv[2], mode, n_threads);
        return 0;
    }
    if (argc >= 3 && mode == "--verify") {
        return run_verify_cmd(sd, argv[2]);
    }
    if (argc >= 3 && mode == "--analyze") {
        run_analysis(sd, argv[2], n_threads);
        return 0;
    }
    if (argc >= 3 && mode == "--polish") {
        auto [n_agv, tree] = load_csv_tree(argv[2]);
        double out_c = 0.0;
        Tree best_t = coordinate_polish(sd, n_agv, tree, out_c, n_threads);
        std::cout << "[Polish] n=" << n_agv << " | w51=" << std::fixed << std::setprecision(4) << out_c << "\n";
        if (argc >= 4) save_csv_tree(sd, argv[3], n_agv, best_t);
        return 0;
    }
    if (mode == "--reconstruct") {
        std::string seed_csv = (argc >= 3) ? argv[2] : "";
        return run_reconstruct(sd, seed_csv, 19, n_threads);
    }

    std::cout << "Usage: agv_opt.exe [--eval <csv> | --eval100 <csv> | --eval300 <csv> | --eval800 <csv> | --verify <csv> | --analyze <csv> | --polish <in.csv> [out.csv] | --reconstruct [seed.csv]]\n";
    return 0;
}
