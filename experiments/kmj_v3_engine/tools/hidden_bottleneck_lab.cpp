#include "../src/common/io.hpp"
#include <iomanip>
#include <map>

// Hidden Bottleneck & Micro-Physics Diagnostic Lab (100 seeds on current v3 ans_c1..c5.csv)
// Exp 1: U95 Tail Incident Root-Cause Anatomy (0-Idle vs Detour vs Junction Spike vs Queue-Behind vs UrgDefer)
// Exp 2: Junction Contention Cascade (>=3 queued at junction) & Cross-Time Breakdown
// Exp 3: Idle Parking Gravity vs Urgent Origin Spatial Mismatch (Deadhead & Starvation)
// Exp 4: Equal-Distance Shortest-Path Tie-Breaking Concentration on Bottleneck Junctions
// Exp 5: Active Tree Leaf Utilization & Vehicle Time-Budget Decomposition

struct JSimRoute {
    int nl = 0;
    std::vector<int> jn;
    std::vector<double> la;
    double total = 0.0;
};

struct JSimCase {
    int nd = 0, nJ = 0;
    std::vector<int> JIDX, JNODE;
    double RATE = 0.0, FAC = 1.2, MEAN = 1.0, LAM = 0.0;
};

struct JSimDir {
    std::string dir;
    std::vector<std::vector<double>> DIST;
    std::vector<std::vector<JSimRoute>> ROUTE;
};

struct JSimArrival { int qo, qd; double qt; int qi; };
struct JSimStream { int seed = 0; std::vector<JSimArrival> arr; };

static JSimCase build_jsim_case(const CaseData& cd) {
    JSimCase jc;
    jc.nd = cd.n_stations;
    std::vector<int> deg(cd.n_nodes, 0);
    for (const auto& e : cd.edges) { deg[e.u]++; deg[e.v]++; }
    jc.JIDX.assign(cd.n_nodes, -1);
    jc.nJ = 0;
    for (int v = 0; v < cd.n_nodes; ++v) {
        if (deg[v] >= 3) { jc.JIDX[v] = jc.nJ++; jc.JNODE.push_back(v); }
    }
    for (const auto& f : cd.flows) jc.RATE += f.rate;
    jc.FAC = 60.0 / cd.at;
    jc.MEAN = 60.0 / (cd.alpha * jc.RATE);
    jc.LAM = cd.alpha * jc.RATE;
    return jc;
}

static JSimDir compile_jsim_dir(const CaseData& cd, const JSimCase& jc, const std::string& bits) {
    JSimDir jd;
    jd.dir = bits;
    const int V = cd.n_nodes;
    const double INF = 1e18;
    std::vector<std::vector<double>> D(V, std::vector<double>(V, INF));
    std::vector<std::vector<int>> NX(V, std::vector<int>(V, -1));
    for (int i = 0; i < V; ++i) { D[i][i] = 0.0; NX[i][i] = i; }
    for (int k = 0; k < cd.n_edges; ++k) {
        int u = cd.edges[k].u, v = cd.edges[k].v;
        if (bits[k] == '1') std::swap(u, v);
        if (cd.edges[k].dist < D[u][v]) { D[u][v] = cd.edges[k].dist; NX[u][v] = v; }
    }
    for (int k = 0; k < V; ++k)
        for (int i = 0; i < V; ++i)
            for (int j = 0; j < V; ++j)
                if (D[i][k] + D[k][j] < D[i][j]) {
                    D[i][j] = D[i][k] + D[k][j];
                    NX[i][j] = NX[i][k];
                }
    jd.DIST.assign(jc.nd, std::vector<double>(jc.nd, 0.0));
    jd.ROUTE.assign(jc.nd, std::vector<JSimRoute>(jc.nd));
    for (int a = 0; a < jc.nd; ++a) {
        for (int b = 0; b < jc.nd; ++b) {
            int pa = cd.pd[a], pb = cd.pd[b];
            jd.DIST[a][b] = D[pa][pb];
            JSimRoute rt;
            rt.total = D[pa][pb];
            int c = pa;
            while (c != pb && c >= 0) {
                c = NX[c][pb];
                if (c != pb && c >= 0 && jc.JIDX[c] >= 0) {
                    rt.jn.push_back(jc.JIDX[c]);
                    rt.la.push_back(D[pa][c]);
                }
            }
            rt.nl = (int)rt.jn.size();
            jd.ROUTE[a][b] = std::move(rt);
        }
    }
    return jd;
}

static JSimStream generate_jsim_stream(const CaseData& cd, const JSimCase& jc, int seed) {
    JSimStream sp;
    sp.seed = seed;
    uint32_t rs = (uint32_t)seed;
    auto R = [&]() -> double {
        rs = rs + 0x6D2B79F5u;
        uint32_t t = (rs ^ (rs >> 15)) * (1u | rs);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    };
    auto EXP = [&]() -> double {
        int k = 0;
        for (;;) {
            double u = R(), prev = u; int m = 1;
            for (;;) { double v = R(); if (v < prev) { prev = v; ++m; } else break; }
            if (m % 2 == 1) return (double)k + u;
            ++k;
        }
    };
    double next = EXP() * jc.MEAN;
    int serial = 0;
    while (next <= cd.t_end) {
        double u = R() * jc.RATE;
        int k = 0;
        while (k < cd.n_flows - 1 && u >= cd.flows[k].rate) { u -= cd.flows[k].rate; ++k; }
        sp.arr.push_back({cd.flows[k].o_st, cd.flows[k].d_st, next, serial++});
        next += EXP() * jc.MEAN;
    }
    return sp;
}

struct UrgRecord {
    double w = 0.0;
    int o_st = 0;
    double de = 0.0;
    double jwait_empty = 0.0;
    int n_idle_at_arr = 0;
    int ql_at_arr = 0;
    int urg_in_q_at_arr = 0;
    int n_idle_at_disp = 0;
    bool disp_delayed_by_imm = false; // w > 0.01
};

struct SeedDiag {
    double sum_w = 0.0;
    int cnt_w = 0;
    std::vector<UrgRecord> urg_recs;

    // Time budget (post-warm, sum over all AGVs, in minutes)
    double t_idle = 0.0, t_empty_drv = 0.0, t_load_drv = 0.0, t_lu = 0.0, t_junc_wait = 0.0, t_junc_occ = 0.0;
    // Junction stats
    std::vector<long long> j_cross;
    std::vector<double> j_wait_sum;
    std::vector<int> j_max_q;
    std::vector<long long> j_cascade_events; // q >= 3 waiting at junction
    // Parking & demand
    std::vector<double> st_idle_time;
    std::vector<long long> st_urg_arr;
    // Tree node & path stats
    std::vector<long long> leaf_hits; // indexed 1..9
    long long imm_dispatches = 0, q_dispatches = 0;
    double sum_de = 0.0, sum_dl = 0.0;
};

static SeedDiag run_diag_seed(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                              const JSimStream& sp, int n, const Tree& tree)
{
    SeedDiag dg;
    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest;
    dg.j_cross.assign(nJ, 0);
    dg.j_wait_sum.assign(nJ, 0.0);
    dg.j_max_q.assign(nJ, 0);
    dg.j_cascade_events.assign(nJ, 0);
    dg.st_idle_time.assign(nd, 0.0);
    dg.st_urg_arr.assign(nd, 0);
    dg.leaf_hits.assign(10, 0);

    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const double T = cd.t_end, warm = cd.warm, FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC, pf = cd.p_occ * FAC;
    const double INF = std::numeric_limits<double>::infinity();

    int vnode[64], vst[64], vph[64], vto[64], vk[64], vro[64], vrd[64], vqi[64];
    double vspd[64], vpos[64], vd1[64], tact[64], vrtm[64], vw_req_t[64], vjwait[64], vde[64];
    double vwq_enter[64];
    int vn_idle_arr[64], vql_arr[64], v_urg_arr[64], vn_idle_disp[64];
    const JSimRoute* vrt[64];
    for (int i = 0; i < n; ++i) {
        vnode[i] = i % nd; vst[i] = IDLE; vph[i] = 0; vto[i] = 0; vk[i] = 0; vro[i] = 0; vrd[i] = 0; vqi[i] = -1;
        vspd[i] = 0; vpos[i] = 0; vd1[i] = 0; tact[i] = INF; vrtm[i] = 0; vw_req_t[i] = 0; vjwait[i] = 0; vde[i] = 0;
        vwq_enter[i] = 0; vn_idle_arr[i] = 0; vql_arr[i] = 0; v_urg_arr[i] = 0; vn_idle_disp[i] = 0; vrt[i] = nullptr;
    }

    int jbusy[128], jh[128], jt[128], jq[128 * 64];
    for (int g = 0; g < nJ; ++g) { jbusy[g] = -1; jh[g] = 0; jt[g] = 0; }

    int qo_[512], qd_[512], qi_[512], q_nidle_arr[512], q_ql_arr[512], q_urg_arr[512];
    double qt_[512];
    int ql = 0, nIdle = n, nWq = 0;
    double t = 0.0, key[16], bk[16];
    int idle[64], qcnt[32];
    const int n_arr = (int)sp.arr.size();
    int aidx = 0;

    auto startLeg = [&](int i) {
        const JSimRoute* rt = vrt[i];
        double d1 = (vk[i] < rt->nl) ? rt->la[vk[i]] : rt->total;
        vst[i] = DRV; vd1[i] = d1; tact[i] = t + (d1 - vpos[i]) / vspd[i] * FAC;
    };
    auto travel = [&](int i, int ph, int to, double spd) {
        vrt[i] = &jd.ROUTE[vnode[i]][to];
        vph[i] = ph; vto[i] = to; vspd[i] = spd; vk[i] = 0; vpos[i] = 0.0;
        startLeg(i);
    };
    auto handle = [&](int i) {
        int st = vst[i];
        if (st == DRV) {
            vpos[i] = vd1[i];
            const JSimRoute* rt = vrt[i];
            if (vk[i] < rt->nl) {
                vst[i] = WQ; tact[i] = INF;
                int j = rt->jn[vk[i]];
                jq[j * 64 + (jt[j]++ & 63)] = i;
                vwq_enter[i] = t;
                nWq++;
                int qsz = jt[j] - jh[j];
                if (t >= warm) {
                    if (qsz > dg.j_max_q[j]) dg.j_max_q[j] = qsz;
                    if (qsz >= 3) dg.j_cascade_events[j]++;
                }
                return;
            }
            vnode[i] = vto[i];
            if (vph[i] == 0) {
                double w = t - vrtm[i];
                if (vrtm[i] >= warm) {
                    dg.sum_w += w;
                    dg.cnt_w++;
                    if (vrd[i] == UD) {
                        dg.urg_recs.push_back({
                            w, vro[i], vde[i], vjwait[i],
                            vn_idle_arr[i], vql_arr[i], v_urg_arr[i],
                            vn_idle_disp[i], (vw_req_t[i] > 0.01)
                        });
                    }
                }
                vst[i] = LD; tact[i] = t + tlf;
            } else {
                vst[i] = UL; tact[i] = t + tuf;
            }
        } else if (st == CRS) {
            jbusy[vrt[i]->jn[vk[i]]] = -1;
            vk[i]++;
            startLeg(i);
        } else if (st == LD) {
            travel(i, 1, vrd[i], cd.vc);
        } else if (st == UL) {
            vst[i] = IDLE; tact[i] = INF; nIdle++;
        }
    };
    auto dispatch = [&]() {
        while (nIdle > 0 && ql > 0) {
            int ni = 0;
            for (int m = 0; m < n; ++m) if (vst[m] == IDLE) idle[ni++] = m;
            for (int m = 0; m < nd; ++m) qcnt[m] = 0;
            for (int m = 0; m < ql; ++m) qcnt[qo_[m]]++;

            int bv = -1, bb = -1, bl = 0, b_last_nid = 1;
            for (int a = 0; a < ni; ++a) {
                int vi = idle[a], vn = vnode[vi];
                const auto& dist_vn = jd.DIST[vn];
                for (int b = 0; b < ql; ++b) {
                    int ro = qo_[b]; double de = dist_vn[ro];
                    int kl = 0, id = 1, last_id = 1;
                    for (int g = 0; g < 8 && id != 0; ++g) {
                        last_id = id;
                        const TreeNode& tn = tree.nodes[id]; int ff = tn.feat, side;
                        if (ff == 0) side = (qd_[b] == UD) ? 1 : 0;
                        else if (ff == 1) side = ((t - qt_[b]) <= tn.th) ? 0 : 1;
                        else { double val = (ff == 2) ? de : (ff == 3) ? jd.DIST[ro][qd_[b]] : (double)qcnt[ro]; side = (val <= tn.th) ? 0 : 1; }
                        key[kl++] = tn.is_min ? (double)side : (double)(1 - side);
                        id = side ? tn.hi : tn.lo;
                    }
                    key[kl++] = de; key[kl++] = qt_[b]; key[kl++] = (double)qi_[b]; key[kl++] = (double)vi;
                    bool less = (bv < 0);
                    if (!less) {
                        int lim = (kl < bl) ? kl : bl;
                        for (int i = 0; i < lim; ++i) {
                            if (key[i] < bk[i]) { less = true; break; }
                            if (key[i] > bk[i]) break;
                        }
                    }
                    if (less) {
                        for (int c = 0; c < kl; ++c) bk[c] = key[c];
                        bl = kl; bv = vi; bb = b; b_last_nid = last_id;
                    }
                }
            }
            if (t >= warm) {
                dg.leaf_hits[b_last_nid]++;
                if (t - qt_[bb] <= 0.005) dg.imm_dispatches++;
                else dg.q_dispatches++;
                dg.sum_de += jd.DIST[vnode[bv]][qo_[bb]];
                dg.sum_dl += jd.DIST[qo_[bb]][qd_[bb]];
            }

            vro[bv] = qo_[bb]; vrd[bv] = qd_[bb]; vrtm[bv] = qt_[bb];
            vde[bv] = jd.DIST[vnode[bv]][qo_[bb]];
            vw_req_t[bv] = t - qt_[bb];
            vjwait[bv] = 0.0;
            vn_idle_arr[bv] = q_nidle_arr[bb];
            vql_arr[bv] = q_ql_arr[bb];
            v_urg_arr[bv] = q_urg_arr[bb];
            vn_idle_disp[bv] = ni;

            for (int m = bb; m < ql - 1; ++m) {
                qo_[m] = qo_[m + 1]; qd_[m] = qd_[m + 1]; qt_[m] = qt_[m + 1]; qi_[m] = qi_[m + 1];
                q_nidle_arr[m] = q_nidle_arr[m + 1]; q_ql_arr[m] = q_ql_arr[m + 1]; q_urg_arr[m] = q_urg_arr[m + 1];
            }
            ql--; nIdle--;
            travel(bv, 0, vro[bv], cd.ve);
        }
    };

    for (;;) {
        double next_arr = (aidx < n_arr) ? sp.arr[aidx].qt : INF;
        double tn = next_arr;
        for (int m = 0; m < n; ++m) if (tact[m] < tn) tn = tact[m];
        if (tn > T) break;

        // Accumulate state durations in [t, tn] overlapping with [warm, T]
        double dt_w = std::max(0.0, std::min(tn, T) - std::max(t, warm));
        if (dt_w > 0.0) {
            for (int m = 0; m < n; ++m) {
                if (vst[m] == IDLE) {
                    dg.t_idle += dt_w;
                    dg.st_idle_time[vnode[m]] += dt_w;
                } else if (vst[m] == DRV) {
                    if (vph[m] == 0) dg.t_empty_drv += dt_w;
                    else dg.t_load_drv += dt_w;
                } else if (vst[m] == WQ) {
                    dg.t_junc_wait += dt_w;
                } else if (vst[m] == CRS) {
                    dg.t_junc_occ += dt_w;
                } else {
                    dg.t_lu += dt_w;
                }
            }
        }

        t = tn;
        while (aidx < n_arr && sp.arr[aidx].qt <= t) {
            int uc = 0;
            for (int m = 0; m < ql; ++m) if (qd_[m] == UD) uc++;
            qo_[ql] = sp.arr[aidx].qo; qd_[ql] = sp.arr[aidx].qd; qt_[ql] = sp.arr[aidx].qt; qi_[ql] = sp.arr[aidx].qi;
            q_nidle_arr[ql] = nIdle; q_ql_arr[ql] = ql; q_urg_arr[ql] = uc;
            if (t >= warm && sp.arr[aidx].qd == UD) dg.st_urg_arr[sp.arr[aidx].qo]++;
            ql++; aidx++;
        }
        for (;;) {
            for (int i = 0; i < n; ++i) while (tact[i] <= t) handle(i);
            if (nWq > 0) {
                for (int g = 0; g < nJ; ++g) {
                    if (jbusy[g] >= 0 || jh[g] == jt[g]) continue;
                    int i = jq[g * 64 + (jh[g]++ & 63)];
                    double wq_dur = t - vwq_enter[i];
                    if (vph[i] == 0) vjwait[i] += wq_dur;
                    if (t >= warm) {
                        dg.j_cross[g]++;
                        dg.j_wait_sum[g] += wq_dur;
                    }
                    vst[i] = CRS; tact[i] = t + pf; jbusy[g] = i; nWq--;
                }
            }
            dispatch();
            bool any = false;
            for (int i = 0; i < n; ++i) if (tact[i] <= t) { any = true; break; }
            if (!any) break;
        }
    }
    return dg;
}

int main() {
    std::cout << std::fixed << std::setprecision(4);
    const int N_SEEDS = 100;

    std::cout << "========================================================================\n";
    std::cout << "HIDDEN BOTTLENECK & MICRO-PHYSICS DIAGNOSTIC LAB (100 seeds, v3 answers)\n";
    std::cout << "========================================================================\n\n";

    for (int c = 1; c <= 5; ++c) {
        CaseData cd = load_case("data/c" + std::to_string(c) + ".json", "c" + std::to_string(c));
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c" + std::to_string(c) + ".csv");
        JSimDir jd = compile_jsim_dir(cd, jc, ans_dir);

        std::vector<JSimStream> sps(N_SEEDS);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < N_SEEDS; ++i) sps[i] = generate_jsim_stream(cd, jc, i + 1);

        std::vector<SeedDiag> diags(N_SEEDS);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < N_SEEDS; ++i) diags[i] = run_diag_seed(cd, jc, jd, sps[i], ans_n, ans_tree);

        // Aggregate
        double tot_w = 0.0; long long tot_cnt = 0;
        std::vector<UrgRecord> all_urg;
        double t_idle = 0, t_edrv = 0, t_ldrv = 0, t_lu = 0, t_jw = 0, t_jo = 0;
        std::vector<long long> j_cross(jc.nJ, 0), j_casc(jc.nJ, 0);
        std::vector<double> j_wsum(jc.nJ, 0.0);
        std::vector<int> j_maxq(jc.nJ, 0);
        std::vector<double> st_idle(jc.nd, 0.0);
        std::vector<long long> st_urg(jc.nd, 0);
        std::vector<long long> leaf_hits(10, 0);
        long long imm_d = 0, q_d = 0;
        double s_de = 0, s_dl = 0;

        for (const auto& d : diags) {
            tot_w += d.sum_w; tot_cnt += d.cnt_w;
            all_urg.insert(all_urg.end(), d.urg_recs.begin(), d.urg_recs.end());
            t_idle += d.t_idle; t_edrv += d.t_empty_drv; t_ldrv += d.t_load_drv;
            t_lu += d.t_lu; t_jw += d.t_junc_wait; t_jo += d.t_junc_occ;
            for (int g = 0; g < jc.nJ; ++g) {
                j_cross[g] += d.j_cross[g];
                j_wsum[g] += d.j_wait_sum[g];
                j_casc[g] += d.j_cascade_events[g];
                j_maxq[g] = std::max(j_maxq[g], d.j_max_q[g]);
            }
            for (int s = 0; s < jc.nd; ++s) {
                st_idle[s] += d.st_idle_time[s];
                st_urg[s] += d.st_urg_arr[s];
            }
            for (int k = 1; k <= 9; ++k) leaf_hits[k] += d.leaf_hits[k];
            imm_d += d.imm_dispatches; q_d += d.q_dispatches;
            s_de += d.sum_de; s_dl += d.sum_dl;
        }

        std::sort(all_urg.begin(), all_urg.end(), [](const UrgRecord& a, const UrgRecord& b) { return a.w < b.w; });
        double u95 = all_urg[(size_t)std::floor(0.95 * (all_urg.size() - 1))].w;
        double mean_w = tot_w / (double)tot_cnt;
        double cost100 = cd.c_agv * ans_n + cd.c_w * jc.LAM * mean_w + cd.c_u * u95;

        std::cout << ">>> CASE " << c << " (n=" << ans_n << ", " << ans_tree.n_nodes << "n, 100-seed Cost=" << cost100
                  << ", W=" << mean_w << ", U95=" << u95 << ") <<<\n";

        // 1. Time Budget
        double t_tot = t_idle + t_edrv + t_ldrv + t_lu + t_jw + t_jo;
        std::cout << "  [1. AGV Time Budget] Idle=" << std::setprecision(1) << (100.0 * t_idle / t_tot) << "% | "
                  << "EmptyDrv=" << (100.0 * t_edrv / t_tot) << "% | "
                  << "LoadedDrv=" << (100.0 * t_ldrv / t_tot) << "% | "
                  << "Load/Unload=" << (100.0 * t_lu / t_tot) << "% | "
                  << "JuncOcc(0.36t)=" << (100.0 * t_jo / t_tot) << "% | "
                  << "JuncWait=" << (100.0 * t_jw / t_tot) << "%\n";
        std::cout << std::setprecision(2)
                  << "  [   Dispatch Flow  ] Imm=" << (100.0 * imm_d / (imm_d + q_d)) << "% (" << imm_d
                  << "), Queue=" << (100.0 * q_d / (imm_d + q_d)) << "% | Avg de=" << (s_de / (imm_d + q_d))
                  << "m, Avg dl=" << (s_dl / (imm_d + q_d)) << "m, DeadheadRatio(de/dl)=" << (s_de / s_dl) << "\n";

        // 2. U95 Tail Anatomy (top 5% urgent orders: w >= u95)
        int tail_cnt = 0, c_zero_idle = 0, c_urg_behind = 0, c_detour = 0, c_jspike = 0, c_other = 0;
        std::map<int, int> tail_by_orig;
        double tail_de_sum = 0.0, tail_jw_sum = 0.0;
        std::vector<double> med_de_list;
        for (const auto& r : all_urg) med_de_list.push_back(r.de);
        std::nth_element(med_de_list.begin(), med_de_list.begin() + med_de_list.size() / 2, med_de_list.end());
        double med_de_u = med_de_list[med_de_list.size() / 2];

        for (const auto& r : all_urg) {
            if (r.w < u95 - 1e-9) continue;
            tail_cnt++;
            tail_by_orig[r.o_st]++;
            tail_de_sum += r.de;
            tail_jw_sum += r.jwait_empty;
            if (r.n_idle_at_arr == 0) {
                if (r.urg_in_q_at_arr > 0) c_urg_behind++;
                else c_zero_idle++;
            } else if (r.jwait_empty >= 0.5) {
                c_jspike++;
            } else if (r.de > std::max(60.0, 1.3 * med_de_u)) {
                c_detour++;
            } else {
                c_other++;
            }
        }
        std::cout << "  [2. U95 Tail Anatomy (n=" << tail_cnt << " orders >= " << std::setprecision(3) << u95 << "m)]\n"
                  << std::setprecision(1)
                  << "      0-Idle Starvation=" << (100.0 * c_zero_idle / tail_cnt) << "% | "
                  << "Urgent Queue-Behind=" << (100.0 * c_urg_behind / tail_cnt) << "% | "
                  << "Far-Pickup Detour=" << (100.0 * c_detour / tail_cnt) << "% | "
                  << "Junc-Wait Spike(>=0.5m)=" << (100.0 * c_jspike / tail_cnt) << "% | "
                  << "Other/Defer=" << (100.0 * c_other / tail_cnt) << "%\n"
                  << std::setprecision(2)
                  << "      Tail Avg de=" << (tail_de_sum / tail_cnt) << "m (vs all-urg med " << med_de_u << "m)"
                  << " | Tail Avg Empty JuncWait=" << (tail_jw_sum / tail_cnt) << "m | Top Tail Origins:";
        std::vector<std::pair<int, int>> orig_v;
        for (auto& kv : tail_by_orig) orig_v.push_back({kv.second, kv.first});
        std::sort(orig_v.rbegin(), orig_v.rend());
        for (int k = 0; k < std::min(3, (int)orig_v.size()); ++k) {
            std::cout << " " << cd.depts[orig_v[k].second] << "(" << std::setprecision(1)
                      << (100.0 * orig_v[k].first / tail_cnt) << "%)";
        }
        std::cout << "\n";

        // 3. Top 3 Junction Cascades & Average Wait
        std::vector<std::tuple<double, int>> j_ord;
        for (int g = 0; g < jc.nJ; ++g) j_ord.push_back({j_wsum[g], g});
        std::sort(j_ord.rbegin(), j_ord.rend());
        std::cout << "  [3. Top 3 Bottleneck Junctions (Wait Sum / Cascade q>=3)]\n";
        for (int k = 0; k < std::min(3, (int)j_ord.size()); ++k) {
            int g = std::get<1>(j_ord[k]);
            double mean_jw = (j_cross[g] > 0) ? (j_wsum[g] / j_cross[g]) : 0.0;
            std::cout << "      #" << (k + 1) << " " << cd.node_names[jc.JNODE[g]]
                      << ": Cross=" << std::setprecision(1) << (j_cross[g] / (double)N_SEEDS) << "/seed"
                      << ", AvgWait=" << std::setprecision(3) << mean_jw << "m"
                      << ", TotWait=" << std::setprecision(1) << (j_wsum[g] / N_SEEDS) << "m/seed"
                      << ", Cascade(q>=3)=" << std::setprecision(2) << (j_casc[g] / (double)N_SEEDS) << "/seed"
                      << ", MaxQ=" << j_maxq[g] << "\n";
        }

        // 4. Spatial Mismatch: Idle Parking Gravity vs Urgent Origin Share
        long long tot_u_arr = 0;
        for (int s = 0; s < jc.nd; ++s) tot_u_arr += st_urg[s];
        std::cout << "  [4. Spatial Mismatch: Idle Parking% vs Urgent Origin% (Worst Deficits)]\n      ";
        std::vector<std::tuple<double, int>> mis;
        for (int s = 0; s < jc.nd; ++s) {
            double p_park = 100.0 * st_idle[s] / std::max(1.0, t_idle);
            double p_u = 100.0 * st_urg[s] / std::max(1LL, tot_u_arr);
            if (p_u > 1.0 || p_park > 12.0) mis.push_back({p_u - p_park, s});
        }
        std::sort(mis.rbegin(), mis.rend());
        for (size_t i = 0; i < std::min<size_t>(4, mis.size()); ++i) {
            int s = std::get<1>(mis[i]);
            double p_park = 100.0 * st_idle[s] / std::max(1.0, t_idle);
            double p_u = 100.0 * st_urg[s] / std::max(1LL, tot_u_arr);
            std::cout << cd.depts[s] << "(Urg " << std::setprecision(1) << p_u << "% vs Park " << p_park
                      << "%, d=" << std::showpos << (p_u - p_park) << std::noshowpos << "%)  ";
        }
        std::cout << "\n";

        // 5. Equal-Distance Tie-Break Audit: How many OD pairs have >= 2 distinct shortest paths?
        int V = cd.n_nodes;
        std::vector<std::vector<double>> D(V, std::vector<double>(V, 1e18));
        std::vector<std::vector<long long>> CNT(V, std::vector<long long>(V, 0));
        for (int i = 0; i < V; ++i) { D[i][i] = 0.0; CNT[i][i] = 1; }
        for (int k = 0; k < cd.n_edges; ++k) {
            int u = cd.edges[k].u, v = cd.edges[k].v;
            if (ans_dir[k] == '1') std::swap(u, v);
            double w = cd.edges[k].dist;
            if (w < D[u][v] - 1e-9) { D[u][v] = w; CNT[u][v] = 1; }
            else if (std::abs(w - D[u][v]) <= 1e-9) CNT[u][v]++;
        }
        for (int k = 0; k < V; ++k)
            for (int i = 0; i < V; ++i)
                for (int j = 0; j < V; ++j) {
                    if (k == i || k == j) continue;
                    double ndist = D[i][k] + D[k][j];
                    if (ndist < D[i][j] - 1e-9) { D[i][j] = ndist; CNT[i][j] = CNT[i][k] * CNT[k][j]; }
                    else if (std::abs(ndist - D[i][j]) <= 1e-9) CNT[i][j] += CNT[i][k] * CNT[k][j];
                }
        int tie_od = 0, tot_od = jc.nd * (jc.nd - 1);
        double tie_flow_pct = 0.0;
        for (int a = 0; a < jc.nd; ++a)
            for (int b = 0; b < jc.nd; ++b)
                if (a != b && CNT[cd.pd[a]][cd.pd[b]] > 1) tie_od++;
        for (const auto& f : cd.flows)
            if (CNT[cd.pd[f.o_st]][cd.pd[f.d_st]] > 1) tie_flow_pct += 100.0 * f.rate / jc.RATE;
        std::cout << "  [5. Equal-Distance Routing Ties] Tied Station Pairs=" << tie_od << "/" << tot_od
                  << " (" << std::setprecision(1) << (100.0 * tie_od / tot_od) << "%), Tied Loaded Flow="
                  << tie_flow_pct << "%\n\n";
    }
    return 0;
}
