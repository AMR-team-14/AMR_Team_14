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

SimMetrics run_sim(const SimData& sd, int stream_idx, int n_agv, const Tree& tree, int q_cutoff = -1) {
    const auto& arrivals = sd.streams[stream_idx];
    const int n_arr = (int)arrivals.size();
    int q_limit = (q_cutoff > 0) ? std::min(q_cutoff, sd.qcap) : sd.qcap;

    // Collect distinct w thresholds used in the tree for exact flow pruning even with w min!
    bool all_w_max = true;
    std::vector<double> w_min_thresh;
    for (int nid = 1; nid <= tree.n_nodes; ++nid) {
        if (tree.nodes[nid].feat == 1 && tree.nodes[nid].is_min) {
            all_w_max = false;
            w_min_thresh.push_back(tree.nodes[nid].th);
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
        int cand_rids[64];
        while (idle_mask && q_len > 0) {
            int rep_pos[7];
            int rep_aid[7];
            int n_rep = 0;
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

            int n_cand = 0;
            if (all_w_max) {
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
                    double w = t - ar.at;
                    int qo = qo_map[ar.o];

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

                    bool better = false;
                    if (bits != best_bits) better = bits < best_bits;
                    else if (de != best_de) better = de < best_de;
                    else if (rid != best_rid) better = rid < best_rid;
                    else better = aid < best_aid;

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

bool is_noop_attach(const Tree& t, int p_nid, bool is_hi, int feat, bool is_min) {
    int urg_state = -1;
    bool dl_gt_25 = false;
    auto dfs = [&](auto& self, int curr, int us, bool dgt) -> bool {
        if (curr == 0) return false;
        const auto& tn = t.nodes[curr];
        if (curr == p_nid) {
            if (tn.feat == 0) us = is_hi ? 1 : 0;
            else if (tn.feat == 3 && tn.th >= 25 && is_hi) dgt = true;
            urg_state = us; dl_gt_25 = dgt;
            return true;
        }
        int lo_u = (tn.feat == 0) ? 0 : us;
        int hi_u = (tn.feat == 0) ? 1 : us;
        bool hi_d = (tn.feat == 3 && tn.th >= 25) ? true : dgt;
        return self(self, tn.lo, lo_u, dgt) || self(self, tn.hi, hi_u, hi_d);
    };
    dfs(dfs, 1, -1, false);
    if (urg_state == 1) {
        if (feat == 3 || feat == 4) return true;
        if (feat == 1 && !is_min) return true;
        if (feat == 2 && is_min) return true;
    }
    if (dl_gt_25 && feat == 0) return true;
    return false;
}

struct Split { int feat; bool is_min; double th; };

// Fast 2-stage robust coordinate polish: screens all single-node changes on 12 streams,
// then evaluates top 8 candidates on all 55 streams!
Tree fast_robust_polish(const SimData& sd, int n_agv, Tree base_t, double& out_cost, int n_threads) {
    static const std::vector<double> DE_GRID = {0, 25, 65, 90, 115, 140, 155, 180};
    static const std::vector<double> DL_GRID = {25, 50, 65, 90};
    static const std::vector<double> QO_GRID = {1, 2, 3, 4, 5};
    static const std::vector<double> W_GRID = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0, 7.0, 8.0};

    int n_tot = (int)sd.streams.size(); // 55 streams
    Tree best_t = canonicalize_tree(base_t);
    double best_c = eval_streams(sd, n_agv, best_t, 0, n_tot);

    bool improved = true;
    while (improved) {
        improved = false;
        std::vector<Tree> cands;
        std::set<std::string> seen;
        seen.insert(tree_key_str(best_t));

        for (int nid = 1; nid <= best_t.n_nodes; ++nid) {
            const auto& tn = best_t.nodes[nid];
            if (tn.feat == 0) continue;
            std::vector<double> grid;
            if (tn.feat == 1) {
                grid = W_GRID;
                for (int d = -4; d <= 4; ++d) {
                    grid.push_back(std::round(std::max(0.1, tn.th + d * 0.1) * 100.0) / 100.0);
                }
                std::sort(grid.begin(), grid.end());
                grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
            } else if (tn.feat == 2) grid = DE_GRID;
            else if (tn.feat == 3) grid = DL_GRID;
            else grid = QO_GRID;

            for (bool cm : {true, false}) {
                for (double cth : grid) {
                    Tree cand = best_t;
                    cand.nodes[nid].is_min = cm; cand.nodes[nid].th = cth;
                    if (is_valid_tree(cand) && seen.insert(tree_key_str(cand)).second) cands.push_back(cand);
                }
            }
            if (tn.lo == 0 && tn.hi == 0 && nid != 1) {
                Tree cand = best_t;
                for (int pid = 1; pid <= cand.n_nodes; ++pid) {
                    if (cand.nodes[pid].lo == nid) cand.nodes[pid].lo = 0;
                    if (cand.nodes[pid].hi == nid) cand.nodes[pid].hi = 0;
                }
                cand = canonicalize_tree(cand);
                if (is_valid_tree(cand) && seen.insert(tree_key_str(cand)).second) cands.push_back(cand);
            }
        }
        if (cands.empty()) break;

        // Stage 1: Screen on 12 streams (seed 11 + unseen 101..111)
        std::vector<double> scr(cands.size(), 999999.0);
        {
            std::vector<std::thread> ths;
            for (int ti = 0; ti < n_threads; ++ti) {
                ths.emplace_back([&, ti]() {
                    for (size_t i = ti; i < cands.size(); i += n_threads) {
                        double c0 = run_sim(sd, 0, n_agv, cands[i], 65).cost;
                        if (c0 < 900000.0) {
                            double c11 = eval_streams(sd, n_agv, cands[i], 5, 16, 65);
                            if (c11 < 900000.0) scr[i] = (c0 + 11.0 * c11) / 12.0;
                        }
                    }
                });
            }
            for (auto& th : ths) th.join();
        }
        std::vector<int> idx(cands.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return scr[a] < scr[b]; });

        // Stage 2: Exact 55-stream eval on top 10
        int top_k = std::min((int)idx.size(), 10);
        std::vector<double> exact_c(top_k, 999999.0);
        {
            std::vector<std::thread> ths;
            for (int ti = 0; ti < n_threads; ++ti) {
                ths.emplace_back([&, ti]() {
                    for (int k = ti; k < top_k; k += n_threads) {
                        if (scr[idx[k]] < 900000.0) {
                            exact_c[k] = eval_streams(sd, n_agv, cands[idx[k]], 0, n_tot);
                        }
                    }
                });
            }
            for (auto& th : ths) th.join();
        }
        for (int k = 0; k < top_k; ++k) {
            if (exact_c[k] < best_c - 1e-4) {
                best_c = exact_c[k];
                best_t = cands[idx[k]];
                improved = true;
            }
        }
    }
    out_cost = best_c;
    return best_t;
}

void run_analysis(const SimData& sd, const std::string& csv_path, int n_threads) {
    auto [base_n, base_t] = load_csv_tree(csv_path);
    std::cout << "=== FULL ANALYSIS FOR " << csv_path << " (n=" << base_n << ", nodes=" << base_t.n_nodes << ") ===\n";
    auto eval_50_unseen = [&](int n_agv, const Tree& tr) {
        int n_unseen = (int)sd.streams.size() - 5;
        std::vector<SimMetrics> ms(n_unseen);
        std::vector<std::thread> ths;
        for (int ti = 0; ti < n_threads; ++ti) {
            ths.emplace_back([&, ti]() {
                for (int i = ti; i < n_unseen; i += n_threads) ms[i] = run_sim(sd, 5 + i, n_agv, tr);
            });
        }
        for (auto& th : ths) th.join();
        return ms;
    };

    std::cout << "\n[1] Public 5-Seed Breakdown (Seeds 11..15):\n";
    double p_sum = 0, p_w = 0, p_u = 0; int p_maxq = 0, p_endq = 0;
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
        double pm_c = 0, pm_w = 0, pm_u = 0; int pm_disq = 0, pm_mq = 0;
        for (int s = 0; s < 5; ++s) {
            SimMetrics m = run_sim(sd, s, na, base_t);
            if (m.cost >= 900000.0) ++pm_disq;
            else { pm_c += m.cost; pm_w += m.w_bar; pm_u += m.u_95; }
            pm_mq = std::max(pm_mq, m.max_q);
        }
        auto ums = eval_50_unseen(na, base_t);
        std::vector<double> ucs; int udisq = 0, umq = 0, ueq = 0;
        double uw = 0, uu = 0;
        for (const auto& m : ums) {
            umq = std::max(umq, m.max_q); ueq = std::max(ueq, m.end_q);
            if (m.cost >= 900000.0) ++udisq;
            else { ucs.push_back(m.cost); uw += m.w_bar; uu += m.u_95; }
        }
        std::sort(ucs.begin(), ucs.end());
        double umean = ucs.empty() ? 999999.0 : std::accumulate(ucs.begin(), ucs.end(), 0.0) / ucs.size();
        double uvar = 0.0; for (double v : ucs) uvar += (v - umean) * (v - umean);
        double ustd = ucs.size() > 1 ? std::sqrt(uvar / ucs.size()) : 0.0;
        double umax = ucs.empty() ? 999999.0 : ucs.back();
        std::cout << "  n=" << std::setw(2) << na << " | pub=" << std::setprecision(2) << (pm_disq ? 999999.0 : pm_c / 5.0)
                  << " (w=" << std::setprecision(4) << (pm_disq ? 999.0 : pm_w / 5.0) << ", u95=" << (pm_disq ? 999.0 : pm_u / 5.0)
                  << ", maxQ=" << pm_mq << ") | unseen50: mean=" << std::setprecision(2) << umean
                  << " (w=" << std::setprecision(4) << (ucs.empty() ? 999.0 : uw / ucs.size())
                  << ", u95=" << (ucs.empty() ? 999.0 : uu / ucs.size()) << ") std=" << std::setprecision(2) << ustd
                  << " max=" << umax << " maxQ=" << umq << " endQ=" << ueq << " disq=" << udisq << "/50\n";
    }

    std::cout << "\n[3] Node Ablation Analysis (Removing each node 2..N):\n";
    double full_pub = p_sum / 5.0;
    auto full_u50 = eval_50_unseen(base_n, base_t);
    double full_u_mean = 0; for (const auto& m : full_u50) full_u_mean += m.cost / full_u50.size();
    for (int rem = 2; rem <= base_t.n_nodes; ++rem) {
        Tree ab = base_t;
        for (int pid = 1; pid <= ab.n_nodes; ++pid) {
            if (ab.nodes[pid].lo == rem) ab.nodes[pid].lo = 0;
            if (ab.nodes[pid].hi == rem) ab.nodes[pid].hi = 0;
        }
        ab = canonicalize_tree(ab);
        double ac = 0, aw = 0, au = 0;
        for (int s = 0; s < 5; ++s) { SimMetrics m = run_sim(sd, s, base_n, ab); ac += m.cost; aw += m.w_bar; au += m.u_95; }
        auto a_u50 = eval_50_unseen(base_n, ab);
        double au_mean = 0; int au_disq = 0;
        for (const auto& m : a_u50) { if (m.cost >= 900000.0) ++au_disq; else au_mean += m.cost; }
        au_mean = (50 - au_disq) ? au_mean / (50 - au_disq) : 999999.0;
        const auto& rn = base_t.nodes[rem];
        std::cout << "  Drop #" << rem << " (" << FEAT_NAMES[rn.feat] << " " << (rn.is_min ? "min" : "max") << " " << rn.th
                  << "): pub=" << std::setprecision(2) << ac / 5.0 << " (delta=" << std::showpos << (ac / 5.0 - full_pub) << std::noshowpos
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
        if (std::find(vals.begin(), vals.end(), tn.th) == vals.end()) { vals.push_back(tn.th); std::sort(vals.begin(), vals.end()); }
        std::cout << "  Node #" << nid << " (" << FEAT_NAMES[tn.feat] << " " << (tn.is_min ? "min" : "max") << ", opt=" << tn.th << "):\n";
        for (double v : vals) {
            Tree cand = base_t; cand.nodes[nid].th = v;
            auto ms = eval_50_unseen(base_n, cand);
            double sum = 0; int dq = 0;
            for (const auto& m : ms) { if (m.cost >= 900000.0) ++dq; else sum += m.cost; }
            double um = (50 - dq) ? sum / (50 - dq) : 999999.0;
            std::cout << "    th=" << std::setw(5) << v << " -> unseen50=" << std::setprecision(2) << um << " (disq=" << dq << ")\n";
        }
    }
}

int main(int argc, char** argv) {
    SimData sd = load_case_data("agv_case_data (1).json", "agv_dispatch_log_seed11 (1).csv", 50);
    int n_threads = 10;

    if (argc >= 3 && std::string(argv[1]) == "--eval") {
        auto [n_agv, tree] = load_csv_tree(argv[2]);
        double sum = 0.0;
        std::cout << "[" << argv[2] << "] n=" << n_agv << ", nodes=" << tree.n_nodes << " | 5-seed:";
        for (int s = 0; s < 5; ++s) {
            double c = run_sim(sd, s, n_agv, tree).cost;
            sum += c;
            std::cout << " " << std::fixed << std::setprecision(4) << c;
        }
        std::cout << " | mean=" << std::setprecision(4) << (sum / 5.0) << "\n";
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--analyze") {
        run_analysis(sd, argv[2], n_threads);
        return 0;
    }

    if (argc >= 2 && std::string(argv[1]) == "--robust") {
        auto t0 = std::chrono::steady_clock::now();
        int n_tot = (int)sd.streams.size(); // 55 streams (0=exact seed11, 1..4=mulberry12..15, 5..54=unseen101..150)

        std::vector<Split> all_splits = {
            {0, false, 0.5},
            {2, true, 0}, {2, true, 25}, {2, true, 65}, {2, true, 90}, {2, true, 115},
            {1, false, 0.5}, {1, false, 1.0}, {1, false, 1.5}, {1, false, 2.0}, {1, false, 2.5}, {1, false, 3.5}, {1, false, 4.5}, {1, false, 6.0},
            {1, true, 0.5}, {1, true, 1.0}, {1, true, 1.5}, {1, true, 2.0}, {1, true, 3.0}, {1, true, 4.0}, {1, true, 5.0},
            {4, false, 1}, {4, false, 2}, {4, false, 3}, {4, true, 1}, {4, true, 2},
            {3, true, 25}, {3, true, 50}, {3, true, 65}, {3, false, 25}, {3, false, 65}
        };

        std::vector<Tree> base_pool;
        for (const char* fn : {"cpp_best_18.csv", "cpp_best_19.csv", "cpp_best_20.csv", "answer_exact_sync.csv", "answer_bruteforce.csv"}) {
            std::ifstream chk(fn);
            if (chk.good()) base_pool.push_back(load_csv_tree(fn).second);
        }
        // Also seed with the 7-node robust core (drop #4 & #7 from cpp_best_19) + fresh 3-node roots
        {
            std::ifstream chk("cpp_best_19.csv");
            if (chk.good()) {
                Tree tr = load_csv_tree("cpp_best_19.csv").second;
                tr.nodes[2].lo = 0;
                base_pool.push_back(canonicalize_tree(tr));
            }
        }
        // Add diverse fresh 3-node skeletons across all root features
        std::vector<Split> r1s = {{2, true, 25}, {2, true, 65}, {1, false, 2.5}, {4, false, 1}, {3, true, 65}, {0, false, 0.5}};
        std::vector<Split> r2s = {{0, false, 0.5}, {4, false, 1}, {3, true, 65}, {1, false, 2.5}, {2, true, 65}};
        for (const auto& r1 : r1s) {
            for (const auto& r2 : r2s) {
                if (r2.feat == r1.feat) continue;
                for (const auto& r3 : r2s) {
                    if (r3.feat == r1.feat) continue;
                    Tree t3; t3.n_nodes = 3;
                    t3.nodes[1] = {r1.feat, r1.is_min, r1.th, 2, 3};
                    t3.nodes[2] = {r2.feat, r2.is_min, r2.th, 0, 0};
                    t3.nodes[3] = {r3.feat, r3.is_min, r3.th, 0, 0};
                    if (is_valid_tree(t3)) base_pool.push_back(t3);
                }
            }
        }

        double global_best_55 = 999999.0;
        int global_best_n = 19;
        Tree global_best_t;

        for (int n_agv : {18, 19, 20}) {
            // Fast 12-stream screen of base_pool -> polish top 6 unique trees on 55 streams
            std::vector<Tree> uniq_base;
            std::set<std::string> bseen;
            for (const auto& bt : base_pool) {
                Tree ct = canonicalize_tree(bt);
                if (is_valid_tree(ct) && bseen.insert(tree_key_str(ct)).second) uniq_base.push_back(ct);
            }
            std::vector<double> bscr(uniq_base.size(), 999999.0);
            {
                std::vector<std::thread> ths;
                for (int ti = 0; ti < n_threads; ++ti) {
                    ths.emplace_back([&, ti]() {
                        for (size_t i = ti; i < uniq_base.size(); i += n_threads) {
                            double c0 = run_sim(sd, 0, n_agv, uniq_base[i], 65).cost;
                            if (c0 < 900000.0) {
                                double c11 = eval_streams(sd, n_agv, uniq_base[i], 5, 16, 65);
                                if (c11 < 900000.0) bscr[i] = (c0 + 11.0 * c11) / 12.0;
                            }
                        }
                    });
                }
                for (auto& th : ths) th.join();
            }
            std::vector<int> bidx(uniq_base.size());
            std::iota(bidx.begin(), bidx.end(), 0);
            std::sort(bidx.begin(), bidx.end(), [&](int a, int b) { return bscr[a] < bscr[b]; });

            std::vector<std::pair<double, Tree>> elites;
            for (int k = 0; k < std::min((int)bidx.size(), 6); ++k) {
                if (bscr[bidx[k]] >= 900000.0) break;
                double rc = 0;
                Tree rt = fast_robust_polish(sd, n_agv, uniq_base[bidx[k]], rc, n_threads);
                elites.push_back({rc, rt});
            }
            std::sort(elites.begin(), elites.end(), [](auto& a, auto& b) { return a.first < b.first; });
            double best_c = elites[0].first;
            Tree best_t = elites[0].second;
            std::cout << "[Robust55 Init n=" << n_agv << "] start mean55=" << std::fixed << std::setprecision(2) << best_c << "\n" << std::flush;

            std::mt19937 rng(2026 + n_agv);
            for (int round = 1; round <= 4; ++round) {
                std::vector<Tree> cands;
                std::set<std::string> seen;
                seen.insert(tree_key_str(best_t));

                int used_e = 0;
                std::set<std::string> se;
                for (const auto& [ec, et] : elites) {
                    if (!se.insert(tree_key_str(et)).second) continue;
                    if (++used_e > 3) break;
                    for (int nid = 1; nid <= et.n_nodes; ++nid) {
                        for (const auto& sp : all_splits) {
                            Tree c = et;
                            c.nodes[nid].feat = sp.feat; c.nodes[nid].is_min = sp.is_min; c.nodes[nid].th = sp.th;
                            if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                        }
                        Tree sw = et;
                        std::swap(sw.nodes[nid].lo, sw.nodes[nid].hi);
                        sw = canonicalize_tree(sw);
                        if (is_valid_tree(sw) && seen.insert(tree_key_str(sw)).second) cands.push_back(sw);

                        if (et.n_nodes < 9) {
                            for (int br = 0; br < 2; ++br) {
                                int ch = (br == 0) ? et.nodes[nid].lo : et.nodes[nid].hi;
                                if (ch != 0) continue;
                                for (const auto& sp : all_splits) {
                                    if (is_noop_attach(et, nid, br == 1, sp.feat, sp.is_min)) continue;
                                    Tree c = et;
                                    int nid_new = c.n_nodes + 1;
                                    c.n_nodes = nid_new;
                                    c.nodes[nid_new] = {sp.feat, sp.is_min, sp.th, 0, 0};
                                    if (br == 0) c.nodes[nid].lo = nid_new; else c.nodes[nid].hi = nid_new;
                                    c = canonicalize_tree(c);
                                    if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                                }
                            }
                        }
                        if (et.nodes[nid].lo == 0 && et.nodes[nid].hi == 0 && nid != 1) {
                            Tree c = et;
                            for (int pid = 1; pid <= c.n_nodes; ++pid) {
                                if (c.nodes[pid].lo == nid) c.nodes[pid].lo = 0;
                                if (c.nodes[pid].hi == nid) c.nodes[pid].hi = 0;
                            }
                            c = canonicalize_tree(c);
                            if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                        }
                    }
                }
                for (int trial = 0; trial < 1500; ++trial) {
                    Tree c = best_t;
                    for (int k = 0; k < 2; ++k) {
                        int nid = 1 + (rng() % c.n_nodes);
                        if ((rng() % 4) == 0) std::swap(c.nodes[nid].lo, c.nodes[nid].hi);
                        else {
                            const auto& sp = all_splits[rng() % all_splits.size()];
                            c.nodes[nid].feat = sp.feat; c.nodes[nid].is_min = sp.is_min; c.nodes[nid].th = sp.th;
                        }
                    }
                    c = canonicalize_tree(c);
                    if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                }

                // Screen on 12 streams -> top 40 on 55 streams -> polish top 4
                std::vector<double> scr(cands.size(), 999999.0);
                {
                    std::vector<std::thread> ths;
                    for (int ti = 0; ti < n_threads; ++ti) {
                        ths.emplace_back([&, ti]() {
                            for (size_t i = ti; i < cands.size(); i += n_threads) {
                                double c0 = run_sim(sd, 0, n_agv, cands[i], 65).cost;
                                if (c0 < 900000.0) {
                                    double c11 = eval_streams(sd, n_agv, cands[i], 5, 16, 65);
                                    if (c11 < 900000.0) scr[i] = (c0 + 11.0 * c11) / 12.0;
                                }
                            }
                        });
                    }
                    for (auto& th : ths) th.join();
                }
                std::vector<int> idx(cands.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(), [&](int a, int b) { return scr[a] < scr[b]; });
                int eval_k = std::min((int)idx.size(), 40);
                std::vector<std::pair<double, Tree>> r55(eval_k);
                {
                    std::vector<std::thread> ths;
                    for (int ti = 0; ti < n_threads; ++ti) {
                        ths.emplace_back([&, ti]() {
                            for (int k = ti; k < eval_k; k += n_threads) {
                                r55[k] = {eval_streams(sd, n_agv, cands[idx[k]], 0, n_tot), cands[idx[k]]};
                            }
                        });
                    }
                    for (auto& th : ths) th.join();
                }
                std::sort(r55.begin(), r55.end(), [](auto& a, auto& b) { return a.first < b.first; });
                for (int k = 0; k < std::min((int)r55.size(), 4); ++k) {
                    if (r55[k].first >= 900000.0) continue;
                    double pc = 0;
                    Tree pt = fast_robust_polish(sd, n_agv, r55[k].second, pc, n_threads);
                    elites.push_back({pc, pt});
                    if (pc < best_c - 1e-4) { best_c = pc; best_t = pt; }
                }
                std::sort(elites.begin(), elites.end(), [](auto& a, auto& b) { return a.first < b.first; });
                std::cout << "  [Robust55 n=" << n_agv << " Round " << round << "] checked=" << cands.size() << " -> mean55=" << best_c << "\n" << std::flush;
            }

            double s11 = run_sim(sd, 0, n_agv, best_t).cost;
            double pub5 = eval_streams(sd, n_agv, best_t, 0, 5);
            double u50 = eval_streams(sd, n_agv, best_t, 5, n_tot);
            save_csv_tree(sd, "robust_best_" + std::to_string(n_agv) + ".csv", n_agv, best_t);
            std::cout << "[Robust Final n=" << n_agv << "] seed11=" << s11 << " | pub5=" << pub5
                      << " | unseen50=" << u50 << " | mean55=" << best_c << " (nodes=" << best_t.n_nodes << ")\n" << std::flush;
            if (best_c < global_best_55) {
                global_best_55 = best_c;
                global_best_n = n_agv;
                global_best_t = best_t;
            }
        }
        save_csv_tree(sd, "answer_final_best.csv", global_best_n, global_best_t);
        auto t1 = std::chrono::steady_clock::now();
        std::cout << "\nSaved overall 55-seed winner to answer_final_best.csv (n=" << global_best_n
                  << ", mean55=" << global_best_55 << ") in " << std::fixed << std::setprecision(2)
                  << std::chrono::duration<double>(t1 - t0).count() << "s!\n";
        return 0;
    }

    std::cout << "Usage: agv_opt.exe [--eval <csv> | --analyze <csv> | --robust]\n";
    return 0;
}
