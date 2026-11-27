#pragma once
#include "case_loader.hpp"

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
