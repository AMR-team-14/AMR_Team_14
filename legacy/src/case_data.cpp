#include "case_data.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace {
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
        if (!f) throw std::runtime_error("Cannot open " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    size_t find_key_pos(const std::string& s, const std::string& key) {
        std::string pat = "\"" + key + "\"";
        size_t pos = s.find(pat);
        if (pos == std::string::npos) throw std::runtime_error("Missing case key: " + key);
        pos = s.find(':', pos + pat.size());
        if (pos == std::string::npos) throw std::runtime_error("Invalid case key: " + key);
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


std::vector<Arrival> decode_reference(const SimData& sd, const std::string& json) {
    std::istringstream input(json_util::parse_all_strings(json).at(0));
    std::vector<Arrival> arrivals;
    long long millis = 0;
    for (std::string token; input >> token;) {
        size_t parsed = 0;
        const long long packed = std::stoll(token, &parsed, 36);
        if (packed < 0 || parsed != token.size()) throw std::runtime_error("Invalid seed 11 reference");
        millis += packed / sd.n_flows;
        const int fid = packed % sd.n_flows;
        const auto& flow = sd.flows.at(fid);
        const int from = sd.pd_nodes.at(flow.o_dept), to = sd.pd_nodes.at(flow.d_dept);
        arrivals.push_back({millis / 1000.0, from, to,
            flow.d_dept == sd.urgent_dest ? 1 : 0, sd.sp_mat[from][to], fid});
    }
    if (arrivals.empty()) throw std::runtime_error("Empty seed 11 reference");
    return arrivals;
}

int append_stream(SimData& sd, uint32_t seed, std::vector<Arrival> stream) {
    std::vector<Event> events;
    events.reserve(stream.size());
    for (size_t i = 0; i < stream.size(); ++i)
        events.push_back({round9(stream[i].at), 0, (int)i});
    sd.seeds.push_back(seed);
    sd.streams.push_back(std::move(stream));
    sd.base_events.push_back(std::move(events));
    return (int)sd.seeds.size() - 1;
}
} // namespace

std::string read_text(const std::string& path) { return json_util::read_all(path); }

int scenario_index(SimData& sd, uint32_t seed) {
    const auto existing = std::find(sd.seeds.begin(), sd.seeds.end(), seed);
    if (existing != sd.seeds.end()) return (int)(existing - sd.seeds.begin());
    return append_stream(sd, seed, generate_arrivals(sd, seed));
}

SimData load_case(const std::string& js, const std::string& seed11_json, int extra_scenarios) {
    SimData sd;

    sd.fixed_dir = json_util::parse_str(js, "fixedDir");
    sd.min_fleet = (int)json_util::parse_num(js, "nMin");
    sd.max_fleet = (int)json_util::parse_num(js, "nMax");
    if (sd.min_fleet < 1 || sd.max_fleet > 30 || sd.min_fleet > sd.max_fleet)
        throw std::runtime_error("Invalid fleet bounds");
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
    if (sd.max_nodes < 1 || sd.max_nodes > 9) throw std::runtime_error("Invalid tree limit");
    sd.c_agv = json_util::parse_num(js, "cAGV");
    sd.c_w = json_util::parse_num(js, "cW");
    sd.c_u = json_util::parse_num(js, "cU");

    if (!std::isfinite(vc) || vc <= 0 || !std::isfinite(ve) || ve <= 0 ||
        !std::isfinite(at) || at <= 0 || !std::isfinite(sd.alpha) || sd.alpha <= 0 ||
        !std::isfinite(sd.t_end) || sd.t_end <= 0)
        throw std::runtime_error("Invalid case timing or speed");
    double scale = 60.0 / at;
    double v_empty = ve / scale;
    double v_loaded = vc / scale;
    sd.t_load = tl * scale;
    sd.t_unload = tu * scale;
    sd.t_inter = p_occ * scale;
    sd.lam_hr = 284.0 * sd.alpha;

    auto pd_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "pd")));
    if (pd_nums.size() != sd.pd_nodes.size()) throw std::runtime_error("Invalid department count");
    for (int i = 0; i < 7; ++i) {
        if (pd_nums[i] < 0 || pd_nums[i] >= sd.n_nodes || std::floor(pd_nums[i]) != pd_nums[i])
            throw std::runtime_error("Invalid department node");
        sd.pd_nodes[i] = (int)pd_nums[i];
    }

    auto flow_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "flows")));
    if (flow_nums.empty() || flow_nums.size() % 3 || flow_nums.size() > 27)
        throw std::runtime_error("Invalid flows");
    sd.n_flows = (int)(flow_nums.size() / 3);
    sd.flows.resize(sd.n_flows);
    for (int i = 0; i < sd.n_flows; ++i) {
        const double from = flow_nums[3 * i], to = flow_nums[3 * i + 1], rate = flow_nums[3 * i + 2];
        if (from < 0 || to < 0 || from >= 7 || to >= 7 || std::floor(from) != from ||
            std::floor(to) != to || !std::isfinite(rate) || rate <= 0)
            throw std::runtime_error("Invalid flow");
        sd.flows[i] = FlowDef{(int)from, (int)to, rate};
    }

    auto edge_nums = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "edges")));
    if (edge_nums.size() % 3 || sd.fixed_dir.size() != edge_nums.size() / 3 ||
        sd.fixed_dir.find_first_not_of("01") != std::string::npos)
        throw std::runtime_error("Invalid fixed directions or edges");
    int n_edges = (int)(edge_nums.size() / 3);
    std::vector<std::vector<std::pair<int, double>>> adj(sd.n_nodes);
    double edge_w[26][26] = {{0.0}};
    int deg[26] = {0};
    for (int i = 0; i < n_edges; ++i) {
        int u = (int)edge_nums[3 * i];
        int v = (int)edge_nums[3 * i + 1];
        double w = edge_nums[3 * i + 2];
        if (u < 0 || u >= sd.n_nodes || v < 0 || v >= sd.n_nodes || !std::isfinite(w) || w <= 0)
            throw std::runtime_error("Invalid edge");
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
            if (prev[end] < 0 && start != end) throw std::runtime_error("Fixed routes are not fully connected");
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

    const auto seeds = json_util::parse_all_numbers(json_util::extract_bracket(js, json_util::find_key_pos(js, "seeds")));
    if (seeds != std::vector<double>{11, 12, 13, 14, 15}) throw std::runtime_error("Case must define seeds 11–15");
    for (double seed : seeds) {
        if (seed == 11) append_stream(sd, 11, decode_reference(sd, seed11_json));
        else scenario_index(sd, (uint32_t)seed);
    }
    for (int i = 0; i < extra_scenarios; ++i) scenario_index(sd, 101 + i);
    return sd;
}
