#pragma once
#include "graph.hpp"

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
struct SimRawPool {
    double sum_w = 0.0;
    int cnt_all = 0;
    std::vector<double> waits_urg;
};
SimMetrics run_sim(const CaseData& cd, const CompiledDir& cdir, const StreamPack& sp,
                   int n_agv, const Tree& tree, int q_cutoff = -1, bool brute_force = false,
                   std::vector<double>* out_wait_samples = nullptr,
                   SimRawPool* out_pool = nullptr) {
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
            if (ar.u == 1) {
                waits_urg.push_back(wt);
                if (out_pool) out_pool->waits_urg.push_back(wt);
            }
            if (out_wait_samples && (rid % 7 == 0)) out_wait_samples->push_back(wt);
        }
    }
    if (out_pool) {
        out_pool->sum_w = sum_w;
        out_pool->cnt_all = cnt_all;
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
