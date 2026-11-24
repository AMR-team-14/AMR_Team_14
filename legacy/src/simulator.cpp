#include "simulator.hpp"
#include <algorithm>

struct AgvState {
    int status = 0;
    int src = 0;
    int dst = 0;
    int hidx = 0;
    bool is_loaded = false;
    int rid = -1;
};

SimMetrics run_sim(const SimData& sd, int stream_idx, int n_agv, const Tree& tree, int q_cutoff) {
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

double eval_streams(const SimData& sd, int n_agv, const Tree& t, int start_s, int end_s, int q_cut) {
    double sum = 0.0;
    int cnt = end_s - start_s;
    for (int s = start_s; s < end_s; ++s) {
        double c = run_sim(sd, s, n_agv, t, q_cut).cost;
        if (c >= 900000.0) return 999999.0;
        sum += c;
    }
    return sum / cnt;
}
