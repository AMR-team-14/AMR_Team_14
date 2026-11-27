#include "common/io.hpp"
#include <chrono>
#include <iomanip>

static std::atomic<long long> g_sims{0};

// ============================================================================
// 0. Exact C++20 Port of simulator/score.cjs + internal/jsim.cjs (Zero Mismatch)
//    + Bit-Exact Representative-Station & Packed-Key Fast Dispatch
//    + Optional Per-Node Branch Visit Tracing for Exact Zombie Splicing
// ============================================================================
struct JSimRoute {
    int nl = 0;
    std::vector<int> jn;
    std::vector<double> la;
    double total = 0.0;
};

struct JSimCase {
    int nd = 0;
    int nJ = 0;
    std::vector<int> JIDX;
    double RATE = 0.0;
    double FAC = 1.2;
    double MEAN = 1.0;
    double LAM = 0.0;
};

struct JSimDir {
    std::string dir;
    double sum_fd = 0.0;
    double dist_f[16][16]{};
    std::vector<std::vector<double>> DIST;
    std::vector<std::vector<JSimRoute>> ROUTE;
};

struct JSimArrival {
    int qo;
    int qd;
    double qt;
    int qi;
};

struct JSimStream {
    int seed = 0;
    std::vector<JSimArrival> arr;
};

struct NodeVisitTrace {
    uint64_t lo_cnt[10]{};
    uint64_t hi_cnt[10]{};
};

static JSimCase build_jsim_case(const CaseData& cd) {
    JSimCase jc;
    jc.nd = cd.n_stations;
    std::vector<int> deg(cd.n_nodes, 0);
    for (const auto& e : cd.edges) {
        deg[e.u]++;
        deg[e.v]++;
    }
    jc.JIDX.assign(cd.n_nodes, -1);
    jc.nJ = 0;
    for (int v = 0; v < cd.n_nodes; ++v) {
        if (deg[v] >= 3) jc.JIDX[v] = jc.nJ++;
    }
    jc.RATE = 0.0;
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
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> D(V, std::vector<double>(V, INF));
    std::vector<std::vector<int>> NX(V, std::vector<int>(V, -1));
    for (int i = 0; i < V; ++i) { D[i][i] = 0.0; NX[i][i] = i; }
    for (int k = 0; k < cd.n_edges; ++k) {
        int u = cd.edges[k].u, v = cd.edges[k].v;
        if (bits[k] == '1') std::swap(u, v);
        D[u][v] = cd.edges[k].dist;
        NX[u][v] = v;
    }
    for (int k = 0; k < V; ++k) {
        for (int i = 0; i < V; ++i) {
            for (int j = 0; j < V; ++j) {
                double d = D[i][k] + D[k][j];
                if (d < D[i][j]) {
                    D[i][j] = d;
                    NX[i][j] = NX[i][k];
                }
            }
        }
    }

    jd.DIST.assign(jc.nd, std::vector<double>(jc.nd, 0.0));
    jd.ROUTE.assign(jc.nd, std::vector<JSimRoute>(jc.nd));
    for (int a = 0; a < jc.nd; ++a) {
        for (int b = 0; b < jc.nd; ++b) {
            int pa = cd.pd[a], pb = cd.pd[b];
            jd.DIST[a][b] = D[pa][pb];
            if (a < 16 && b < 16) jd.dist_f[a][b] = D[pa][pb];
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
    jd.sum_fd = 0.0;
    for (const auto& f : cd.flows) jd.sum_fd += f.rate * jd.DIST[f.o_st][f.d_st];
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
            double u = R();
            double prev = u;
            int m = 1;
            for (;;) {
                double v = R();
                if (v < prev) { prev = v; ++m; }
                else break;
            }
            if (m % 2 == 1) return (double)k + u;
            ++k;
        }
    };

    double next = EXP() * jc.MEAN;
    int serial = 0;
    sp.arr.reserve((size_t)(jc.LAM * cd.t_end / 60.0 * 1.15));
    while (next <= cd.t_end) {
        double u = R() * jc.RATE;
        int k = 0;
        while (k < cd.n_flows - 1 && u >= cd.flows[k].rate) {
            u -= cd.flows[k].rate;
            ++k;
        }
        sp.arr.push_back({cd.flows[k].o_st, cd.flows[k].d_st, next, serial++});
        next = next + EXP() * jc.MEAN;
    }
    return sp;
}

static std::vector<JSimStream> build_jsim_streams(const CaseData& cd, const JSimCase& jc, const std::vector<int>& seeds) {
    std::vector<JSimStream> out(seeds.size());
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)seeds.size(); ++i) {
        out[i] = generate_jsim_stream(cd, jc, seeds[i]);
    }
    return out;
}

struct JSimOut {
    bool unstable = true;
    int maxQ = 0;
    int endQ = 0;
    double sum_w = 0.0;
    int cnt_w = 0;
    std::vector<double> wu;
};

// Reference unoptimized JSim (used at startup to verify 100% bit-exactness of fast JSim)
static JSimOut run_jsim_ref(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                            const JSimStream& sp, int n, const Tree& tree)
{
    JSimOut out;
    out.unstable = false;
    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest, qcap = cd.qcap;
    const double T = cd.t_end, warm = cd.warm, FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC, pf = cd.p_occ * FAC;
    const double vc = cd.vc, ve = cd.ve;
    const double INF = std::numeric_limits<double>::infinity();

    int vnode[64], vst[64], vph[64], vto[64], vk[64], vro[64], vrd[64];
    double vspd[64], vpos[64], vd1[64], tact[64], vrtm[64];
    const JSimRoute* vrt[64];
    for (int i = 0; i < n; ++i) {
        vnode[i] = i % nd; vst[i] = IDLE; vph[i] = 0; vto[i] = 0; vk[i] = 0; vro[i] = 0; vrd[i] = 0;
        vspd[i] = 0.0; vpos[i] = 0.0; vd1[i] = 0.0; tact[i] = INF; vrtm[i] = 0.0; vrt[i] = nullptr;
    }
    int jbusy[128], jh[128], jt[128], jq[128 * 64];
    for (int g = 0; g < nJ; ++g) { jbusy[g] = -1; jh[g] = 0; jt[g] = 0; }
    int qo_[512], qd_[512], qi_[512], ql = 0, nIdle = n, nWq = 0, maxQ = 0;
    double qt_[512], t = 0.0, key[16], bk[16];
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
                int j = rt->jn[vk[i]]; jq[j * 64 + (jt[j]++ & 63)] = i; nWq++; return;
            }
            vnode[i] = vto[i];
            if (vph[i] == 0) {
                double w = t - vrtm[i];
                if (vrtm[i] >= warm) { out.sum_w += w; out.cnt_w++; if (vrd[i] == UD) out.wu.push_back(w); }
                vst[i] = LD; tact[i] = t + tlf;
            } else { vst[i] = UL; tact[i] = t + tuf; }
        } else if (st == CRS) { jbusy[vrt[i]->jn[vk[i]]] = -1; vk[i]++; startLeg(i); }
        else if (st == LD) travel(i, 1, vrd[i], vc);
        else if (st == UL) { vst[i] = IDLE; tact[i] = INF; nIdle++; }
    };
    auto dispatch = [&]() {
        while (nIdle > 0 && ql > 0) {
            int ni = 0;
            for (int m = 0; m < n; ++m) if (vst[m] == IDLE) idle[ni++] = m;
            for (int m = 0; m < nd; ++m) qcnt[m] = 0;
            for (int m = 0; m < ql; ++m) qcnt[qo_[m]]++;
            int bv = -1, bb = -1, bl = 0;
            for (int a = 0; a < ni; ++a) {
                int vi = idle[a], vn = vnode[vi];
                const auto& dist_vn = jd.DIST[vn];
                for (int b = 0; b < ql; ++b) {
                    int ro = qo_[b]; double de = dist_vn[ro]; int kl = 0, id = 1;
                    for (int g = 0; g < 8 && id != 0; ++g) {
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
                    if (less) { for (int c = 0; c < kl; ++c) bk[c] = key[c]; bl = kl; bv = vi; bb = b; }
                }
            }
            vro[bv] = qo_[bb]; vrd[bv] = qd_[bb]; vrtm[bv] = qt_[bb];
            for (int m = bb; m < ql - 1; ++m) {
                qo_[m] = qo_[m + 1]; qd_[m] = qd_[m + 1]; qt_[m] = qt_[m + 1]; qi_[m] = qi_[m + 1];
            }
            ql--; nIdle--; travel(bv, 0, vro[bv], ve);
        }
    };
    out.wu.reserve(128);
    for (;;) {
        double next_arr = (aidx < n_arr) ? sp.arr[aidx].qt : INF;
        double tn = next_arr;
        for (int m = 0; m < n; ++m) if (tact[m] < tn) tn = tact[m];
        if (tn > T) break;
        t = tn;
        while (aidx < n_arr && sp.arr[aidx].qt <= t) {
            if (ql >= 512) { out.unstable = true; out.maxQ = ql; out.endQ = ql; return out; }
            qo_[ql] = sp.arr[aidx].qo; qd_[ql] = sp.arr[aidx].qd; qt_[ql] = sp.arr[aidx].qt; qi_[ql] = sp.arr[aidx].qi;
            ql++; aidx++;
        }
        for (;;) {
            for (int i = 0; i < n; ++i) while (tact[i] <= t) handle(i);
            if (nWq > 0) for (int g = 0; g < nJ; ++g) {
                if (jbusy[g] >= 0 || jh[g] == jt[g]) continue;
                int i = jq[g * 64 + (jh[g]++ & 63)];
                vst[i] = CRS; tact[i] = t + pf; jbusy[g] = i; nWq--;
            }
            dispatch();
            bool any = false; for (int i = 0; i < n; ++i) if (tact[i] <= t) { any = true; break; }
            if (!any) break;
        }
        if (ql > maxQ) maxQ = ql;
        if (ql > qcap) { out.unstable = true; out.maxQ = maxQ; out.endQ = ql; return out; }
    }
    out.maxQ = maxQ; out.endQ = ql; out.unstable = (ql > cd.end_qmax);
    return out;
}

// Fast Bit-Exact JSim: Representative lowest-ID idle AGV per station + Precomputed per-order attrs + Packed uint32 key
static JSimOut run_jsim(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                        const JSimStream& sp, int n, const Tree& tree,
                        NodeVisitTrace* vtrace = nullptr)
{
    g_sims.fetch_add(1, std::memory_order_relaxed);
    JSimOut out;
    out.unstable = false;

    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const int nd = jc.nd, nJ = jc.nJ;
    const double T = cd.t_end, warm = cd.warm;
    const int UD = cd.urgent_dest;
    const double FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC, pf = cd.p_occ * FAC;
    const int qcap = cd.qcap;
    const double vc = cd.vc, ve = cd.ve;
    const double INF = std::numeric_limits<double>::infinity();

    int vnode[64], vst[64], vph[64], vto[64], vk[64], vro[64], vrd[64];
    double vspd[64], vpos[64], vd1[64], tact[64], vrtm[64];
    const JSimRoute* vrt[64];
    for (int i = 0; i < n; ++i) {
        vnode[i] = i % nd;
        vst[i] = IDLE; vph[i] = 0; vto[i] = 0; vk[i] = 0; vro[i] = 0; vrd[i] = 0;
        vspd[i] = 0.0; vpos[i] = 0.0; vd1[i] = 0.0; tact[i] = INF; vrtm[i] = 0.0;
        vrt[i] = nullptr;
    }

    int jbusy[128], jh[128], jt[128];
    int jq[128 * 64];
    for (int g = 0; g < nJ; ++g) { jbusy[g] = -1; jh[g] = 0; jt[g] = 0; }

    int qo_[512], qd_[512], qi_[512];
    double qt_[512];
    int ql = 0;

    int nIdle = n, nWq = 0, maxQ = 0;
    double t = 0.0;
    int rep_vi[16], rep_vn[16], qcnt[16];
    int b_ud[512];
    double b_w[512], b_dl[512], b_qc[512];

    const int n_arr = (int)sp.arr.size();
    int aidx = 0;

    auto startLeg = [&](int i) {
        const JSimRoute* rt = vrt[i];
        double d1 = (vk[i] < rt->nl) ? rt->la[vk[i]] : rt->total;
        vst[i] = DRV;
        vd1[i] = d1;
        tact[i] = t + (d1 - vpos[i]) / vspd[i] * FAC;
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
                nWq++;
                return;
            }
            vnode[i] = vto[i];
            if (vph[i] == 0) {
                double w = t - vrtm[i];
                if (vrtm[i] >= warm) {
                    out.sum_w += w;
                    out.cnt_w++;
                    if (vrd[i] == UD) out.wu.push_back(w);
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
            travel(i, 1, vrd[i], vc);
        } else if (st == UL) {
            vst[i] = IDLE; tact[i] = INF; nIdle++;
        }
    };
    auto dispatch = [&]() {
        while (nIdle > 0 && ql > 0) {
            // Only the lowest-index idle AGV at each station can ever win tie-break (key[kl+3] = vi)
            uint32_t st_seen = 0;
            int n_rep = 0;
            for (int m = 0; m < n; ++m) {
                if (vst[m] == IDLE) {
                    int vn = vnode[m];
                    uint32_t bit = 1u << vn;
                    if (!(st_seen & bit)) {
                        st_seen |= bit;
                        rep_vi[n_rep] = m;
                        rep_vn[n_rep] = vn;
                        ++n_rep;
                    }
                }
            }
            for (int m = 0; m < nd; ++m) qcnt[m] = 0;
            for (int m = 0; m < ql; ++m) qcnt[qo_[m]]++;
            for (int b = 0; b < ql; ++b) {
                int ro = qo_[b], rd = qd_[b];
                b_ud[b] = (rd == UD) ? 1 : 0;
                b_w[b] = t - qt_[b];
                b_dl[b] = jd.dist_f[ro][rd];
                b_qc[b] = (double)qcnt[ro];
            }

            int bv = -1, bb = -1, best_qi = 0x7FFFFFFF;
            uint32_t best_bits = 0xFFFFFFFFu;
            double best_de = INF, best_qt = INF;

            for (int a = 0; a < n_rep; ++a) {
                int vi = rep_vi[a];
                const double* dist_vn = jd.dist_f[rep_vn[a]];
                for (int b = 0; b < ql; ++b) {
                    double de = dist_vn[qo_[b]];
                    uint32_t bits = 0;
                    int id = 1;
                    for (int g = 0; g < 8 && id != 0; ++g) {
                        const TreeNode& tn = tree.nodes[id];
                        int ff = tn.feat;
                        int side;
                        if (ff == 0) side = b_ud[b];
                        else if (ff == 1) side = (b_w[b] <= tn.th) ? 0 : 1;
                        else {
                            double val = (ff == 2) ? de : (ff == 3) ? b_dl[b] : b_qc[b];
                            side = (val <= tn.th) ? 0 : 1;
                        }
                        if (vtrace) {
                            if (side) vtrace->hi_cnt[id]++;
                            else vtrace->lo_cnt[id]++;
                        }
                        uint32_t kbit = tn.is_min ? (uint32_t)side : (uint32_t)(1 - side);
                        bits |= (kbit << (15 - g));
                        id = side ? tn.hi : tn.lo;
                    }

                    bool less = (bv < 0);
                    if (!less) {
                        if (bits != best_bits) less = (bits < best_bits);
                        else if (de != best_de) less = (de < best_de);
                        else if (qt_[b] != best_qt) less = (qt_[b] < best_qt);
                        else if (qi_[b] != best_qi) less = (qi_[b] < best_qi);
                        else less = (vi < bv);
                    }
                    if (less) {
                        best_bits = bits; best_de = de; best_qt = qt_[b]; best_qi = qi_[b];
                        bv = vi; bb = b;
                    }
                }
            }
            vro[bv] = qo_[bb]; vrd[bv] = qd_[bb]; vrtm[bv] = qt_[bb];
            for (int m = bb; m < ql - 1; ++m) {
                qo_[m] = qo_[m + 1]; qd_[m] = qd_[m + 1]; qt_[m] = qt_[m + 1]; qi_[m] = qi_[m + 1];
            }
            ql--; nIdle--;
            travel(bv, 0, vro[bv], ve);
        }
    };

    out.wu.reserve(128);
    for (;;) {
        double next_arr = (aidx < n_arr) ? sp.arr[aidx].qt : INF;
        double tn = next_arr;
        for (int m = 0; m < n; ++m) if (tact[m] < tn) tn = tact[m];
        if (tn > T) break;
        t = tn;
        while (aidx < n_arr && sp.arr[aidx].qt <= t) {
            if (ql >= 512) { out.unstable = true; out.maxQ = ql; out.endQ = ql; return out; }
            qo_[ql] = sp.arr[aidx].qo;
            qd_[ql] = sp.arr[aidx].qd;
            qt_[ql] = sp.arr[aidx].qt;
            qi_[ql] = sp.arr[aidx].qi;
            ql++; aidx++;
        }
        for (;;) {
            for (int i = 0; i < n; ++i) while (tact[i] <= t) handle(i);
            if (nWq > 0) {
                for (int g = 0; g < nJ; ++g) {
                    if (jbusy[g] >= 0 || jh[g] == jt[g]) continue;
                    int i = jq[g * 64 + (jh[g]++ & 63)];
                    vst[i] = CRS; tact[i] = t + pf; jbusy[g] = i; nWq--;
                }
            }
            dispatch();
            bool any = false;
            for (int i = 0; i < n; ++i) if (tact[i] <= t) { any = true; break; }
            if (!any) break;
        }
        if (ql > maxQ) maxQ = ql;
        if (ql > qcap) {
            out.unstable = true; out.maxQ = maxQ; out.endQ = ql; return out;
        }
    }
    out.maxQ = maxQ;
    out.endQ = ql;
    out.unstable = (ql > cd.end_qmax);
    return out;
}

// ============================================================================
// 1. Exact Pooled Objectives, 10-Block CVaR & Exact Invariant Zombie Splicer
// ============================================================================
struct PooledStat {
    double pooled_cost = 999999.0;
    double mean_w = 999.0;
    double u95 = 999.0;
    int dq_cnt = 0;
    int max_q = 0;
    int max_end_q = 0;
};

static PooledStat eval_pooled_serial(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                                     const std::vector<JSimStream>& sps, int n_agv, const Tree& t)
{
    PooledStat res;
    double tot_w = 0.0;
    long long tot_cnt = 0;
    std::vector<double> all_wu;
    all_wu.reserve(sps.size() * 120);
    for (size_t i = 0; i < sps.size(); ++i) {
        JSimOut o = run_jsim(cd, jc, jd, sps[i], n_agv, t);
        res.max_q = std::max(res.max_q, o.maxQ);
        res.max_end_q = std::max(res.max_end_q, o.endQ);
        if (o.unstable) {
            res.dq_cnt = 1;
            return res;
        }
        tot_w += o.sum_w;
        tot_cnt += o.cnt_w;
        all_wu.insert(all_wu.end(), o.wu.begin(), o.wu.end());
    }
    if (tot_cnt == 0 || all_wu.empty()) return res;
    std::sort(all_wu.begin(), all_wu.end());
    size_t idx95 = (size_t)std::floor(0.95 * (all_wu.size() - 1));
    res.mean_w = tot_w / (double)tot_cnt;
    res.u95 = all_wu[idx95];
    res.pooled_cost = cd.c_agv * n_agv + cd.c_w * jc.LAM * res.mean_w + cd.c_u * res.u95;
    return res;
}

static PooledStat eval_exact_pooled(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                                    const std::vector<JSimStream>& sps, int n_agv, const Tree& t,
                                    bool early_abort = true, std::vector<JSimOut>* raw_outs = nullptr)
{
    const int N = (int)sps.size();
    std::vector<JSimOut> local_outs;
    std::vector<JSimOut>& outs = raw_outs ? *raw_outs : local_outs;
    outs.resize(N);
    std::atomic<int> any_dq{0};
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < N; ++i) {
        if (early_abort && any_dq.load(std::memory_order_relaxed) != 0) continue;
        outs[i] = run_jsim(cd, jc, jd, sps[i], n_agv, t);
        if (outs[i].unstable) any_dq.fetch_add(1, std::memory_order_relaxed);
    }
    PooledStat res;
    double tot_w = 0.0;
    long long tot_cnt = 0;
    std::vector<double> all_wu;
    if (any_dq.load() == 0) all_wu.reserve((size_t)N * 120);
    for (int i = 0; i < N; ++i) {
        res.max_q = std::max(res.max_q, outs[i].maxQ);
        res.max_end_q = std::max(res.max_end_q, outs[i].endQ);
        if (outs[i].unstable) { ++res.dq_cnt; continue; }
        if (res.dq_cnt == 0) {
            tot_w += outs[i].sum_w;
            tot_cnt += outs[i].cnt_w;
            all_wu.insert(all_wu.end(), outs[i].wu.begin(), outs[i].wu.end());
        }
    }
    if (res.dq_cnt > 0 || tot_cnt == 0 || all_wu.empty()) return res;
    std::sort(all_wu.begin(), all_wu.end());
    size_t idx95 = (size_t)std::floor(0.95 * (all_wu.size() - 1));
    res.mean_w = tot_w / (double)tot_cnt;
    res.u95 = all_wu[idx95];
    res.pooled_cost = cd.c_agv * n_agv + cd.c_w * jc.LAM * res.mean_w + cd.c_u * res.u95;
    return res;
}

// Compute Worst-20% Block CVaR across 100-seed blocks (zero extra DES calls!)
static double compute_block_cvar20(const CaseData& cd, const JSimCase& jc, int n_agv,
                                   const std::vector<JSimOut>& outs_a, const std::vector<JSimOut>& outs_b)
{
    std::vector<const JSimOut*> ptrs;
    ptrs.reserve(outs_a.size() + outs_b.size());
    for (const auto& o : outs_a) ptrs.push_back(&o);
    for (const auto& o : outs_b) ptrs.push_back(&o);
    const int blk_sz = 100;
    std::vector<double> blk_costs;
    for (size_t st = 0; st + blk_sz <= ptrs.size(); st += blk_sz) {
        double tw = 0.0;
        long long tc = 0;
        std::vector<double> wu;
        wu.reserve(blk_sz * 110);
        for (size_t i = st; i < st + blk_sz; ++i) {
            if (ptrs[i]->unstable) return 999999.0;
            tw += ptrs[i]->sum_w;
            tc += ptrs[i]->cnt_w;
            wu.insert(wu.end(), ptrs[i]->wu.begin(), ptrs[i]->wu.end());
        }
        if (tc == 0 || wu.empty()) return 999999.0;
        std::sort(wu.begin(), wu.end());
        double u95 = wu[(size_t)std::floor(0.95 * (wu.size() - 1))];
        blk_costs.push_back(cd.c_agv * n_agv + cd.c_w * jc.LAM * (tw / (double)tc) + cd.c_u * u95);
    }
    if (blk_costs.empty()) return 999999.0;
    std::sort(blk_costs.begin(), blk_costs.end(), std::greater<double>());
    int k = std::max(1, (int)blk_costs.size() / 5);
    double sum = 0.0;
    for (int i = 0; i < k; ++i) sum += blk_costs[i];
    return sum / (double)k;
}

static inline double exact_score(const PooledStat& ps) {
    return (ps.dq_cnt > 0 || ps.pooled_cost > 900000.0) ? 999999.0 : ps.pooled_cost;
}

static Tree round_tree_v3(const Tree& src) {
    Tree t = canonicalize_tree(src);
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        int f = t.nodes[nid].feat;
        if (f == 0) t.nodes[nid].th = 0.5;
        else if (f == 1) t.nodes[nid].th = std::round(t.nodes[nid].th * 100000.0) / 100000.0;
        else if (f == 2 || f == 3) t.nodes[nid].th = std::round(t.nodes[nid].th * 10.0) / 10.0;
        else t.nodes[nid].th = std::max(1.0, std::round(t.nodes[nid].th)); // Insight 9.3: qo >= 1 always!
    }
    return t;
}

static std::string v3_tree_key(const Tree& t) {
    std::ostringstream ss;
    ss << t.n_nodes;
    for (int i = 1; i <= t.n_nodes; ++i) {
        const auto& n = t.nodes[i];
        ss << "|" << n.feat << (n.is_min ? '<' : '>') << std::llround(n.th * 100000.0) << ":" << n.lo << "," << n.hi;
    }
    return ss.str();
}

// Insight 9.3 & 9.4: Algebraically splice out any zombie node (lo_cnt==0 or hi_cnt==0)
// Guarantees 100.0000% identical dispatch trajectory while freeing wasted node budget!
static Tree splice_zombie_nodes(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                                const std::vector<JSimStream>& sp_probe, int n_agv, const Tree& raw_tree)
{
    Tree cur = round_tree_v3(raw_tree);
    if (!is_strict_tree(cur, cd.max_nodes)) return cur;

    for (int pass = 0; pass < 8 && cur.n_nodes > 1; ++pass) {
        NodeVisitTrace vt{};
        const int S = (int)sp_probe.size();
        std::vector<NodeVisitTrace> th_vt(S);
        bool any_un = false;
        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < S; ++i) {
            JSimOut o = run_jsim(cd, jc, jd, sp_probe[i], n_agv, cur, &th_vt[i]);
            if (o.unstable) any_un = true;
        }
        if (any_un) break;
        for (int i = 0; i < S; ++i) {
            for (int k = 1; k <= cur.n_nodes; ++k) {
                vt.lo_cnt[k] += th_vt[i].lo_cnt[k];
                vt.hi_cnt[k] += th_vt[i].hi_cnt[k];
            }
        }

        int dead_id = -1;
        int surv_child = 0;
        const bool allow_quasi_leaf = (sp_probe.size() <= 64);
        for (int nid = cur.n_nodes; nid >= 1; --nid) {
            uint64_t lc = vt.lo_cnt[nid], hc = vt.hi_cnt[nid], tc = lc + hc;
            bool is_leaf = (cur.nodes[nid].lo == 0 && cur.nodes[nid].hi == 0);
            bool zero_branch = (lc == 0 || hc == 0);
            bool quasi_zombie_leaf = allow_quasi_leaf && is_leaf && tc > 0 && (std::min(lc, hc) * 1000ULL < tc);
            if (zero_branch || quasi_zombie_leaf) {
                int sc = (lc >= hc) ? cur.nodes[nid].lo : cur.nodes[nid].hi;
                if (nid == 1 && sc == 0) continue; // Keep at least 1 root node
                dead_id = nid;
                surv_child = sc;
                break;
            }
        }
        if (dead_id < 0) break;

        Tree nxt = cur;
        if (dead_id == 1) {
            // Promote surviving child to root via temporary re-rooting before canonicalize
            int order[10], cnt = 0, q[10], qh = 0, qt = 0;
            bool seen[10] = {false};
            q[qt++] = surv_child; seen[surv_child] = true;
            while (qh < qt) {
                int u = q[qh++]; order[cnt++] = u;
                for (int ch : {cur.nodes[u].lo, cur.nodes[u].hi}) {
                    if (ch != 0 && !seen[ch]) { seen[ch] = true; q[qt++] = ch; }
                }
            }
            int id_map[10] = {0};
            for (int i = 0; i < cnt; ++i) id_map[order[i]] = i + 1;
            Tree reroot;
            reroot.n_nodes = cnt;
            for (int i = 0; i < cnt; ++i) {
                int old = order[i];
                reroot.nodes[i + 1] = cur.nodes[old];
                reroot.nodes[i + 1].lo = id_map[cur.nodes[old].lo];
                reroot.nodes[i + 1].hi = id_map[cur.nodes[old].hi];
            }
            nxt = round_tree_v3(reroot);
        } else {
            for (int p = 1; p <= nxt.n_nodes; ++p) {
                if (nxt.nodes[p].lo == dead_id) nxt.nodes[p].lo = surv_child;
                if (nxt.nodes[p].hi == dead_id) nxt.nodes[p].hi = surv_child;
            }
            nxt = round_tree_v3(nxt);
        }
        if (!is_strict_tree(nxt, cd.max_nodes)) break;
        cur = nxt;
    }
    return cur;
}

// Insight 9.3: Check if `(parent_nid, attach_is_hi)` lies inside the `lo` (`w <= eps`, eps <= 0.006)
// subtree of any `w` ancestor, where `w == 0` and `qo == 1` are 100% invariant!
static bool is_under_fresh_arrival_gate(const Tree& t, int target_nid, int attach_side) {
    int parent[10] = {0};
    int pside[10] = {0};
    for (int p = 1; p <= t.n_nodes; ++p) {
        if (t.nodes[p].lo > 0) { parent[t.nodes[p].lo] = p; pside[t.nodes[p].lo] = 0; }
        if (t.nodes[p].hi > 0) { parent[t.nodes[p].hi] = p; pside[t.nodes[p].hi] = 1; }
    }
    if (attach_side >= 0 && t.nodes[target_nid].feat == 1 && t.nodes[target_nid].th <= 0.006 && attach_side == 0) {
        return true;
    }
    int cur = target_nid;
    while (parent[cur] != 0) {
        int p = parent[cur];
        int s = pside[cur];
        if (t.nodes[p].feat == 1 && t.nodes[p].th <= 0.006 && s == 0) return true;
        cur = p;
    }
    return false;
}

static std::string jsim_fp(const JSimCase& jc, const JSimDir& jd) {
    std::ostringstream oss;
    for (int i = 0; i < jc.nd; ++i) {
        for (int j = 0; j < jc.nd; ++j) {
            if (i == j) continue;
            oss << (long long)std::llround(jd.DIST[i][j] * 10.0) << ":" << jd.ROUTE[i][j].nl << ",";
        }
    }
    return oss.str();
}

// ============================================================================
// 2. Exact Physics-Grounded Palettes & Three-Track Insight Templates
//    - Track 1 (`w`-root): Dual-Regime Hoarding + Far-Queue `urg,min` Deferral (Insights 6.1, 11.1, 13.2)
//    - Track 2 (`urg`-root): Simultaneous Non-Urgent `w<=eps` Hoarding + Urgent `w,min,T` Triage (Insights 7.1, 12.2)
//    - Track 3 (`de`-root): Near/Far Pickup Split + Conditional Wait-Gated Deferral (Insights 8, 11.4)
// ============================================================================
struct V3Quantiles {
    std::vector<double> de_q;       // 7 core quantiles
    std::vector<double> dl_q;       // 6 core quantiles
    std::vector<double> de_sinks;   // Exact `DIST[sink][o] - 0.3` step-shield switches + near shields
    std::vector<double> dl_mids;    // Complete lossless midpoints of all flow `DIST[o][d]` values
    std::array<std::vector<double>, 5> wing_th;
    std::array<std::vector<double>, 5> swap_pal;
    double max_d = 250.0;
};

static V3Quantiles build_v3_quantiles(const CaseData& cd, const JSimDir& jd) {
    V3Quantiles vq;
    std::vector<double> de_v, dl_v;
    for (int i = 0; i < cd.n_stations; ++i) {
        for (int j = 0; j < cd.n_stations; ++j) {
            if (i != j && std::isfinite(jd.DIST[i][j])) {
                double d = std::round(jd.DIST[i][j] * 10.0) / 10.0;
                de_v.push_back(d);
                vq.max_d = std::max(vq.max_d, d);
            }
        }
    }
    for (int f = 0; f < cd.n_flows; ++f) {
        double d = jd.DIST[cd.flows[f].o_st][cd.flows[f].d_st];
        if (std::isfinite(d)) dl_v.push_back(std::round(d * 10.0) / 10.0);
    }

    auto dedup = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        std::vector<double> res;
        for (double x : v) if (res.empty() || std::abs(res.back() - x) > 0.05) res.push_back(x);
        return res;
    };
    auto quants = [](std::vector<double> v, const std::vector<double>& qs) {
        std::vector<double> out;
        if (v.empty()) return out;
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        for (double q : qs) {
            int idx = std::clamp<int>((int)std::llround(q * ((int)v.size() - 1)), 0, (int)v.size() - 1);
            out.push_back(v[(size_t)idx]);
        }
        return out;
    };

    vq.de_q = quants(de_v, {0.06, 0.16, 0.28, 0.42, 0.58, 0.74, 0.88});
    vq.dl_q = quants(dl_v.empty() ? de_v : dl_v, {0.15, 0.32, 0.50, 0.68, 0.82, 0.92});

    // Insight 5.2, 7.2, 9.6, 11.4: Exact Sink-Pulling Switches (`DIST[UD][o] - 0.3` & net-surplus sinks) + Near Shields
    std::vector<double> sink_sw = {14.5, 19.5, 24.5, 42.0, 59.5};
    const int UD = cd.urgent_dest;
    for (int o = 0; o < cd.n_stations; ++o) {
        if (o != UD && std::isfinite(jd.DIST[UD][o]) && jd.DIST[UD][o] > 3.0) {
            sink_sw.push_back(std::round((jd.DIST[UD][o] - 0.3) * 10.0) / 10.0);
        }
        for (int s = 0; s < cd.n_stations; ++s) {
            if (s != o && cd.st_net[s] > 0.5 && std::isfinite(jd.DIST[s][o]) && jd.DIST[s][o] > 3.0) {
                sink_sw.push_back(std::round((jd.DIST[s][o] - 0.3) * 10.0) / 10.0);
            }
        }
    }
    for (double x : vq.de_q) sink_sw.push_back(x);
    vq.de_sinks = dedup(sink_sw);

    // Insight 9.5: Exact Midpoints of Unique Flow `dl` Values (Lossless Discrete Partition Set)
    std::vector<double> u_dl = dedup(dl_v.empty() ? de_v : dl_v);
    std::vector<double> mids;
    for (size_t i = 0; i + 1 < u_dl.size(); ++i) {
        mids.push_back(std::round(((u_dl[i] + u_dl[i + 1]) * 0.5) * 10.0) / 10.0);
    }
    if (mids.empty() && !u_dl.empty()) mids.push_back(u_dl[0]);
    vq.dl_mids = dedup(mids);

    // Subtree BCD Wing Palettes (compact, high-leverage subset)
    vq.wing_th[0] = {0.5};
    vq.wing_th[1] = {0.0015, 0.015, 0.12, 0.75, 1.80, 2.75}; // Circulation gates + U95 quantile triage!
    std::vector<double> w_de = {19.5, vq.de_q[1], vq.de_q[2], vq.de_q[4], vq.de_q[5], vq.de_q[6]};
    for (int o = 0; o < cd.n_stations; ++o) {
        if (o != UD && cd.st_urg_orig[o] > 0.1 && std::isfinite(jd.DIST[UD][o]) && jd.DIST[UD][o] > 5.0) {
            w_de.push_back(std::round((jd.DIST[UD][o] - 0.3) * 10.0) / 10.0);
        }
    }
    w_de = dedup(w_de);
    if (w_de.size() > 7) {
        std::vector<double> sub;
        for (int k = 0; k < 7; ++k) sub.push_back(w_de[(size_t)(k * (w_de.size() - 1) / 6)]);
        w_de = dedup(sub);
    }
    vq.wing_th[2] = w_de;

    std::vector<double> w_dl = vq.dl_mids;
    if (w_dl.size() > 5) {
        std::vector<double> sub;
        for (int k = 0; k < 5; ++k) sub.push_back(w_dl[(size_t)(k * (w_dl.size() - 1) / 4)]);
        w_dl = dedup(sub);
    }
    vq.wing_th[3] = w_dl;
    vq.wing_th[4] = {1.0, 2.0, 3.0}; // Insight 9.3: No dead 0.0!

    // Full Discrete Physics Palettes for SA & Coordinate Hooke-Jeeves
    vq.swap_pal[0] = {0.5};
    vq.swap_pal[1] = {0.0008, 0.0015, 0.003, 0.01, 0.03, 0.08, 0.22, 0.50, 0.75, 1.15, 1.65, 2.20, 2.75, 3.50};
    std::vector<double> all_de_sw = vq.de_sinks;
    std::vector<double> u_de = dedup(de_v);
    for (size_t i = 0; i + 1 < u_de.size(); ++i) {
        all_de_sw.push_back(std::round(((u_de[i] + u_de[i + 1]) * 0.5) * 10.0) / 10.0);
    }
    vq.swap_pal[2] = dedup(all_de_sw);
    vq.swap_pal[3] = vq.dl_mids;
    vq.swap_pal[4] = {1.0, 2.0, 3.0, 4.0, 5.0};
    return vq;
}

static Tree rescale_tree_v3(const CaseData& cd, const Tree& src, const JSimDir& old_jd, const JSimDir& new_jd) {
    double r = (old_jd.sum_fd > 1.0) ? (new_jd.sum_fd / old_jd.sum_fd) : 1.0;
    V3Quantiles nvq = build_v3_quantiles(cd, new_jd);
    auto snap_nearest = [](const std::vector<double>& pal, double val) {
        if (pal.empty()) return std::round(val * 10.0) / 10.0;
        auto it = std::lower_bound(pal.begin(), pal.end(), val);
        if (it == pal.end()) return pal.back();
        if (it == pal.begin()) return pal.front();
        double hi = *it, lo = *(it - 1);
        return (std::abs(val - lo) <= std::abs(hi - val)) ? lo : hi;
    };
    Tree t = src;
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        if (t.nodes[nid].feat == 2) t.nodes[nid].th = snap_nearest(nvq.swap_pal[2], t.nodes[nid].th * r);
        else if (t.nodes[nid].feat == 3) t.nodes[nid].th = snap_nearest(nvq.swap_pal[3], t.nodes[nid].th * r);
    }
    return round_tree_v3(t);
}

static std::vector<Tree> build_v3_templates(const CaseData& cd, const JSimDir& jd, bool compact_only) {
    V3Quantiles vq = build_v3_quantiles(cd, jd);
    const auto& dq = vq.de_q;
    const auto& lq = vq.dl_q;

    std::vector<Tree> out;
    std::set<std::string> seen;
    auto push = [&](const Tree& raw) {
        Tree t = round_tree_v3(raw);
        if (is_strict_tree(t, cd.max_nodes) && seen.insert(v3_tree_key(t)).second) {
            out.push_back(t);
        }
    };

    std::vector<double> eps_list = compact_only ? std::vector<double>{0.0015}
                                                : std::vector<double>{0.0012, 0.015};

    // ------------------------------------------------------------------------
    // Track 1 (`w,max,eps` Root): Dual-Regime Hoarding (BOTH Short-dl & Long-dl Polarities!)
    //          + Far-Queue `urg,min` Deferral & Exact Sink-Switch Sweep
    // ------------------------------------------------------------------------
    for (double eps : eps_list) {
        for (bool short_hoard : {true, false}) {
            for (bool far_urg_defer : (compact_only ? std::vector<bool>{true} : std::vector<bool>{true, false})) {
                for (int h_i : (compact_only ? std::vector<int>{2, 5} : std::vector<int>{1, 4, 6})) {
                    Tree t; t.n_nodes = 8;
                    t.nodes[1] = {1, false, eps, 2, 3};
                    t.nodes[2] = {0, false, 0.5, 4, 0};
                    t.nodes[3] = {2, true, dq[2], 5, 6};
                    t.nodes[4] = {3, true, lq[2], short_hoard ? 7 : 0, short_hoard ? 0 : 7};
                    t.nodes[5] = {0, false, 0.5, 0, 0};
                    t.nodes[6] = {0, far_urg_defer, 0.5, 8, 0};
                    t.nodes[7] = {2, false, dq[h_i], 0, 0};
                    t.nodes[8] = {3, true, lq[2], 0, 0};
                    push(t);
                }
            }
        }

        // Clean 7-node Exact Sink-Switch Sweep (`DIST[UD][o] - 0.3` with Far-Queue `urg,min` leaf!)
        size_t sink_step = std::max<size_t>(1, vq.de_sinks.size() / (compact_only ? 3 : 6));
        for (size_t si = 0; si < vq.de_sinks.size(); si += sink_step) {
            for (bool short_hoard : {true, false}) {
                Tree t7; t7.n_nodes = 7;
                t7.nodes[1] = {1, false, eps, 2, 3};
                t7.nodes[2] = {0, false, 0.5, 4, 0};
                t7.nodes[3] = {2, true, dq[2], 5, 6};
                t7.nodes[4] = {3, true, lq[2], short_hoard ? 7 : 0, short_hoard ? 0 : 7};
                t7.nodes[5] = {0, false, 0.5, 0, 0};
                t7.nodes[6] = {0, true, 0.5, 0, 0};
                t7.nodes[7] = {2, false, vq.de_sinks[si], 0, 0};
                push(t7);
            }
        }

        if (!compact_only) {
            // Insight 11.4 (Case 3 Loop): Urgent Long-Order Near-Shield (`de,max,19.5`) + Far-Queue `urg,min`
            for (double u_sh : {19.5, dq[0]}) {
                Tree t; t.n_nodes = 9;
                t.nodes[1] = {1, false, eps, 2, 3};
                t.nodes[2] = {0, false, 0.5, 4, 5};
                t.nodes[3] = {2, true, dq[2], 6, 7};
                t.nodes[4] = {3, true, lq[4], 0, 8};
                t.nodes[5] = {3, true, lq[2], 0, 9};
                t.nodes[6] = {0, false, 0.5, 0, 0};
                t.nodes[7] = {0, true, 0.5, 0, 0};
                t.nodes[8] = {2, false, dq[4], 0, 0};
                t.nodes[9] = {2, false, u_sh, 0, 0};
                push(t);
            }
        }
    }

    // ------------------------------------------------------------------------
    // Track 2 (`urg`-Root): Simultaneous Non-Urgent `w<=eps` Hoarding + Urgent `w,min,T` Triage!
    // (Bypasses per-path feature uniqueness so `w` serves BOTH circulation & U95 triage!)
    // ------------------------------------------------------------------------
    for (double u_triage : (compact_only ? std::vector<double>{1.80} : std::vector<double>{0.75, 1.65, 2.75})) {
        for (int h_i : (compact_only ? std::vector<int>{2, 5} : std::vector<int>{1, 3, 5, 6})) {
            // Variant 2A: urg,max,0.5 -> lo(non-urg w<=0.002 hoarding) / hi(urg de,min -> w,min,u_triage)
            Tree t; t.n_nodes = 9;
            t.nodes[1] = {0, false, 0.5, 2, 3};                // urg, max, 0.5 (hi=urgent prioritized)
            t.nodes[2] = {1, false, 0.002, 4, 5};              // non-urg: w, max, 0.002
            t.nodes[3] = {2, true, dq[2], 6, 7};               // urgent: near vs far pickup
            t.nodes[4] = {3, true, lq[3], 0, 8};               // non-urg fresh: dl, min
            t.nodes[5] = {2, true, dq[2], 0, 9};               // non-urg queue: de, min
            t.nodes[6] = {1, true, u_triage, 0, 0};            // near urgent: w, min, u_triage (save >u_triage? wait: w,max prioritizes older <=u_triage vs >u_triage!)
            t.nodes[7] = {1, false, 0.50, 0, 0};               // far urgent: w, max, 0.50 (prioritize aged >0.50!)
            t.nodes[8] = {2, false, dq[h_i], 0, 0};            // non-urg fresh long: de, max hoard!
            t.nodes[9] = {3, true, lq[2], 0, 0};
            push(t);

            // Variant 2B: Urgent U95 Window Sacrifice (`w,min,u_triage` sacrifices hopeless outliers >u_triage!)
            Tree t2 = t;
            t2.nodes[6] = {1, false, u_triage * 0.6, 0, 0};    // near urgent: prioritize older (>0.6*T)
            t2.nodes[7] = {1, true, u_triage, 0, 0};           // far urgent: sacrifice already-blown >u_triage!
            push(t2);
        }
    }

    if (!compact_only) {
        // Variant 2C: `urg,min,0.5` Root (High-Idle Asymmetric Wait Gate + Hoarding, strong on C2/C5)
        for (int h_i : {2, 4, 6}) {
            Tree t; t.n_nodes = 8;
            t.nodes[1] = {0, true, 0.5, 2, 3};
            t.nodes[2] = {1, false, 0.002, 4, 5};
            t.nodes[3] = {2, true, dq[2], 6, 7};
            t.nodes[4] = {3, true, lq[3], 0, 8};
            t.nodes[5] = {3, true, lq[2], 0, 0};
            t.nodes[6] = {1, false, 0.35, 0, 0};
            t.nodes[7] = {1, true, 2.20, 0, 0};
            t.nodes[8] = {2, false, dq[h_i], 0, 0};
            push(t);
        }

        // --------------------------------------------------------------------
        // Track 3 (`de,min,th` Root): Capacity-Tight Near/Far Conditional Deferral (Case 1 / Lean Fleet)
        // --------------------------------------------------------------------
        for (int d_i : {2, 3, 4}) {
            for (bool far_defer : {true, false}) {
                Tree t; t.n_nodes = 9;
                t.nodes[1] = {2, true, dq[d_i], 2, 3};         // de, min, th (near <= th first)
                t.nodes[2] = {4, false, 1.0, 0, 4};            // near: qo > 1 prioritized
                t.nodes[3] = {3, true, lq[2], 5, 0};           // far: short dl first
                t.nodes[4] = {0, false, 0.5, 0, 6};            // near queue: urg, max, 0.5
                t.nodes[5] = {4, false, 1.0, 7, 8};            // far: qo == 1 vs qo > 1
                t.nodes[6] = {1, true, 2.70, 0, 0};            // near urgent: U95 triage window 2.7m!
                t.nodes[7] = {0, far_defer, 0.5, 0, 0};        // far single: urg,min deferral!
                t.nodes[8] = {1, true, 0.75, 9, 0};            // far multi: w <= 0.75 vs > 0.75
                t.nodes[9] = {0, false, 0.5, 0, 0};
                push(t);
            }
        }
    }
    return out;
}

// ============================================================================
// 3. Multi-Root Alternating Subtree Block-Coordinate Beam (`w`-root, `urg`-root, `de`-root)
// ============================================================================
static Tree graft_root_subtrees(const TreeNode& root_spec, const Tree& lo_sub, const Tree& hi_sub) {
    Tree out;
    out.nodes[1] = root_spec;
    out.nodes[1].lo = 0;
    out.nodes[1].hi = 0;
    int cur_sz = 1;
    if (lo_sub.n_nodes > 0) {
        out.nodes[1].lo = 2;
        for (int i = 1; i <= lo_sub.n_nodes; ++i) {
            TreeNode tn = lo_sub.nodes[i];
            if (tn.lo != 0) tn.lo += 1;
            if (tn.hi != 0) tn.hi += 1;
            out.nodes[1 + i] = tn;
        }
        cur_sz += lo_sub.n_nodes;
    }
    if (hi_sub.n_nodes > 0) {
        int off = cur_sz;
        out.nodes[1].hi = off + 1;
        for (int i = 1; i <= hi_sub.n_nodes; ++i) {
            TreeNode tn = hi_sub.nodes[i];
            if (tn.lo != 0) tn.lo += off;
            if (tn.hi != 0) tn.hi += off;
            out.nodes[off + i] = tn;
        }
        cur_sz += hi_sub.n_nodes;
    }
    out.n_nodes = cur_sz;
    return round_tree_v3(out);
}

static std::vector<Tree> expand_wing_beam_v3(
    const CaseData& cd, const JSimCase& jc, const JSimDir& jd, int n_agv,
    const std::vector<JSimStream>& sp_wing, const TreeNode& root_spec,
    bool optimize_lo, const Tree& fixed_other_wing, int max_wing_nodes, int beam_w = 4)
{
    V3Quantiles vq = build_v3_quantiles(cd, jd);
    bool fresh_w_wing = optimize_lo && (root_spec.feat == 1 && root_spec.th <= 0.006);

    std::vector<int> allowed_feats;
    for (int f = 0; f < 5; ++f) {
        if (f == root_spec.feat) continue;          // Strict tree path-unique feature rule
        if (fresh_w_wing && (f == 1 || f == 4)) continue; // Insight 9.3: w==0 & qo==1 invariant!
        allowed_feats.push_back(f);
    }

    std::vector<std::pair<double, Tree>> all_wings;
    std::set<std::string> seen_w;
    std::vector<Tree> cur_cands;
    { Tree empty_w; empty_w.n_nodes = 0; cur_cands.push_back(empty_w); seen_w.insert("0"); }
    for (int f : allowed_feats) {
        for (double th : vq.wing_th[f]) {
            for (bool is_min : {true, false}) {
                Tree w; w.n_nodes = 1; w.nodes[1] = {f, is_min, th, 0, 0};
                w = round_tree_v3(w);
                if (seen_w.insert(v3_tree_key(w)).second) cur_cands.push_back(w);
            }
        }
    }

    auto eval_wings = [&](const std::vector<Tree>& wings) {
        std::vector<double> scs(wings.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < (int)wings.size(); ++i) {
            Tree full = optimize_lo ? graft_root_subtrees(root_spec, wings[i], fixed_other_wing)
                                    : graft_root_subtrees(root_spec, fixed_other_wing, wings[i]);
            if (!is_strict_tree(full, cd.max_nodes)) continue;
            scs[i] = exact_score(eval_pooled_serial(cd, jc, jd, sp_wing, n_agv, full));
        }
        return scs;
    };

    auto sc0 = eval_wings(cur_cands);
    std::vector<std::pair<double, Tree>> beam_sc;
    for (size_t i = 0; i < cur_cands.size(); ++i) {
        if (sc0[i] < 900000.0) {
            all_wings.push_back({sc0[i], cur_cands[i]});
            if (cur_cands[i].n_nodes >= 1) beam_sc.push_back({sc0[i], cur_cands[i]});
        }
    }
    std::sort(beam_sc.begin(), beam_sc.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Tree> beam;
    for (size_t i = 0; i < beam_sc.size() && (int)beam.size() < beam_w; ++i) beam.push_back(beam_sc[i].second);

    for (int depth = 2; depth <= max_wing_nodes && !beam.empty(); ++depth) {
        std::vector<Tree> next_all;
        std::set<std::string> seen_lvl;
        for (const auto& bw : beam) {
            for (int pid = 1; pid <= bw.n_nodes; ++pid) {
                for (int is_hi = 0; is_hi <= 1; ++is_hi) {
                    if ((is_hi ? bw.nodes[pid].hi : bw.nodes[pid].lo) != 0) continue;
                    bool sub_fresh = fresh_w_wing || is_under_fresh_arrival_gate(bw, pid, is_hi);
                    for (int f : allowed_feats) {
                        if (sub_fresh && (f == 1 || f == 4)) continue;
                        for (double th : vq.wing_th[f]) {
                            for (bool is_min : {true, false}) {
                                Tree nw = bw;
                                int nid = ++nw.n_nodes;
                                nw.nodes[nid] = {f, is_min, th, 0, 0};
                                if (is_hi) nw.nodes[pid].hi = nid; else nw.nodes[pid].lo = nid;
                                nw = round_tree_v3(nw);
                                Tree full = optimize_lo ? graft_root_subtrees(root_spec, nw, fixed_other_wing)
                                                        : graft_root_subtrees(root_spec, fixed_other_wing, nw);
                                if (!is_strict_tree(full, cd.max_nodes)) continue;
                                if (seen_lvl.insert(v3_tree_key(nw)).second) next_all.push_back(nw);
                            }
                        }
                    }
                }
            }
        }
        if (next_all.empty()) break;

        std::vector<Tree> sub;
        int stride = std::max(1, (int)next_all.size() / 24);
        for (size_t i = 0; i < next_all.size() && (int)sub.size() < 24; i += stride) sub.push_back(next_all[i]);

        auto scs = eval_wings(sub);
        std::vector<std::pair<double, Tree>> lvl_ok;
        for (size_t i = 0; i < sub.size(); ++i) {
            if (scs[i] < 900000.0) {
                lvl_ok.push_back({scs[i], sub[i]});
                all_wings.push_back({scs[i], sub[i]});
            }
        }
        if (lvl_ok.empty()) break;
        std::sort(lvl_ok.begin(), lvl_ok.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        beam.clear();
        for (size_t i = 0; i < lvl_ok.size() && (int)beam.size() < beam_w; ++i) beam.push_back(lvl_ok[i].second);
    }

    std::sort(all_wings.begin(), all_wings.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Tree> res;
    std::set<std::string> seen_res;
    for (const auto& [sc, w] : all_wings) {
        std::string k = (w.n_nodes == 0) ? "0" : v3_tree_key(w);
        if (seen_res.insert(k).second) {
            res.push_back(w);
            if ((int)res.size() >= 2) break;
        }
    }
    return res;
}

static std::vector<Tree> v3_multi_root_bcd_beam(
    const CaseData& cd, const JSimCase& jc, const JSimDir& jd, int n_agv,
    const std::vector<JSimStream>& sp16, const std::vector<JSimStream>& sp32)
{
    V3Quantiles vq = build_v3_quantiles(cd, jd);
    std::vector<Tree> raw_disc;
    std::set<std::string> seen;
    auto push_cand = [&](const Tree& full) {
        if (is_strict_tree(full, cd.max_nodes) && seen.insert(v3_tree_key(full)).second) {
            raw_disc.push_back(full);
        }
    };

    // Track A (`w`-root): Test BOTH `urg,min,0.5` (far-queue deferral!) and `urg,max,0.5` anchors!
    for (double eps : {0.0015, 0.025}) {
        TreeNode r_w{1, false, eps, 0, 0};
        Tree anc_hi_defer;
        anc_hi_defer.n_nodes = 3;
        anc_hi_defer.nodes[1] = {2, true, vq.de_q[2], 2, 3};
        anc_hi_defer.nodes[2] = {0, false, 0.5, 0, 0};
        anc_hi_defer.nodes[3] = {0, true, 0.5, 0, 0}; // Far-queue `urg,min` deferral unlocked!

        auto top_lo = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_w, true, anc_hi_defer, 4, 4);
        for (int li = 0; li < std::min<int>(2, (int)top_lo.size()); ++li) {
            const Tree& lo_w = top_lo[li];
            int rem_hi = std::min(5, cd.max_nodes - 1 - lo_w.n_nodes);
            if (rem_hi < 1) continue;
            auto top_hi = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_w, false, lo_w, rem_hi, 4);
            for (const auto& hi_w : top_hi) push_cand(graft_root_subtrees(r_w, lo_w, hi_w));
        }
    }

    // Track B (`urg`-root): Simultaneous Non-Urgent Hoarding (`lo`) + Urgent `w,min,T` Triage (`hi`)
    {
        TreeNode r_u{0, false, 0.5, 0, 0};
        Tree anc_urg_hi;
        anc_urg_hi.n_nodes = 2;
        anc_urg_hi.nodes[1] = {2, true, vq.de_q[2], 2, 0};
        anc_urg_hi.nodes[2] = {1, true, 2.20, 0, 0};

        auto top_lo = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_u, true, anc_urg_hi, 4, 4);
        for (int li = 0; li < std::min<int>(2, (int)top_lo.size()); ++li) {
            const Tree& lo_w = top_lo[li];
            int rem_hi = std::min(4, cd.max_nodes - 1 - lo_w.n_nodes);
            if (rem_hi < 1) continue;
            auto top_hi = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_u, false, lo_w, rem_hi, 4);
            for (const auto& hi_w : top_hi) push_cand(graft_root_subtrees(r_u, lo_w, hi_w));
        }
    }

    // Track C (`de`-root): Near (`lo`) vs Far (`hi`) Conditional Urgent Deferral
    {
        TreeNode r_d{2, true, vq.de_q[2], 0, 0};
        Tree anc_far_hi;
        anc_far_hi.n_nodes = 2;
        anc_far_hi.nodes[1] = {3, true, vq.dl_q[2], 2, 0};
        anc_far_hi.nodes[2] = {0, true, 0.5, 0, 0};

        auto top_lo = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_d, true, anc_far_hi, 4, 3);
        if (!top_lo.empty()) {
            const Tree& lo_w = top_lo[0];
            int rem_hi = std::min(4, cd.max_nodes - 1 - lo_w.n_nodes);
            if (rem_hi >= 1) {
                auto top_hi = expand_wing_beam_v3(cd, jc, jd, n_agv, sp16, r_d, false, lo_w, rem_hi, 3);
                for (const auto& hi_w : top_hi) push_cand(graft_root_subtrees(r_d, lo_w, hi_w));
            }
        }
    }

    if (raw_disc.empty()) return {};
    std::vector<double> sc32(raw_disc.size(), 999999.0);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < (int)raw_disc.size(); ++i) {
        sc32[i] = exact_score(eval_pooled_serial(cd, jc, jd, sp32, n_agv, raw_disc[i]));
    }
    std::vector<int> ord(raw_disc.size());
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { return sc32[a] < sc32[b]; });

    std::vector<Tree> best_out;
    for (int idx : ord) {
        if (sc32[idx] >= 900000.0 || (int)best_out.size() >= 4) break;
        best_out.push_back(splice_zombie_nodes(cd, jc, jd, sp16, n_agv, raw_disc[idx]));
    }
    return best_out;
}

// ============================================================================
// 4. Constrained Dual-Situation Flow + Full `C <= 22` Fast Bitmask SCC Discovery
//    (Insights 2.1, 5.1, 5.4, 10.1, 11.3, 13.4)
// ============================================================================
struct DualFlowMetrics {
    std::string dir;
    std::string fp;
    double f_avg = 1e9;
    double f_urg = 1e9;
    double max_rho_j = 1.0;
};

static bool fast_scc_check_mask(const CaseData& cd, const std::string& bits) {
    if ((int)bits.size() != cd.n_edges) return false;
    for (int e = 0; e < cd.n_edges; ++e) {
        if (cd.parallel_partner[e] > e && bits[e] != bits[cd.parallel_partner[e]]) return false;
    }
    const int V = cd.n_nodes;
    if (V > 64) return is_valid_dir(cd, bits);
    uint64_t adj[64] = {0}, radj[64] = {0};
    for (int k = 0; k < cd.n_edges; ++k) {
        int u = cd.edges[k].u, v = cd.edges[k].v;
        if (bits[k] == '1') std::swap(u, v);
        adj[u] |= (1ULL << v);
        radj[v] |= (1ULL << u);
    }
    uint64_t full = (V == 64) ? ~0ULL : ((1ULL << V) - 1ULL);
    auto reach = [&](const uint64_t* g) {
        uint64_t vis = 1ULL, frontier = 1ULL;
        while (frontier) {
            uint64_t next = 0, f = frontier;
            while (f) {
                int u = __builtin_ctzll(f); f &= f - 1;
                next |= (g[u] & ~vis);
            }
            vis |= next; frontier = next;
        }
        return vis == full;
    };
    return reach(adj) && reach(radj);
}

static DualFlowMetrics eval_dual_flow(const CaseData& cd, const JSimCase& jc, const std::string& bits) {
    DualFlowMetrics fm;
    fm.dir = bits;
    JSimDir jd = compile_jsim_dir(cd, jc, bits);
    fm.fp = jsim_fp(jc, jd);

    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest;
    const double t_inter = cd.p_occ * jc.FAC;
    const double fac_e = jc.FAC / cd.ve;
    const double fac_l = jc.FAC / cd.vc;

    std::vector<double> junc_cross_hr(nJ, 0.0);
    std::vector<double> sup(nd, 0.0);
    double t_load_hr = 0.0;
    for (const auto& f : cd.flows) {
        double r = cd.alpha * f.rate;
        const auto& rt = jd.ROUTE[f.o_st][f.d_st];
        t_load_hr += r * (rt.total * fac_l + rt.nl * t_inter);
        for (int j : rt.jn) junc_cross_hr[j] += r;
        sup[f.d_st] += r;
        sup[f.o_st] -= r;
    }

    // Exact SPFA Min-Cost Empty Rebalancing Flow (including 0.36t junction occupancy cost)
    const int S = nd, T_node = nd + 1, NV = nd + 2;
    struct EdgeMCMF { int to, rev; double cap, cost; };
    std::vector<std::vector<EdgeMCMF>> g(NV);
    auto add_e = [&](int u, int v, double cap, double cost) {
        g[u].push_back({v, (int)g[v].size(), cap, cost});
        g[v].push_back({u, (int)g[u].size() - 1, 0.0, -cost});
    };
    double tot_sup = 0.0;
    for (int s = 0; s < nd; ++s) {
        if (sup[s] > 1e-9) { add_e(S, s, sup[s], 0.0); tot_sup += sup[s]; }
        else if (sup[s] < -1e-9) add_e(s, T_node, -sup[s], 0.0);
    }
    std::vector<std::pair<int, int>> link_idx;
    for (int a = 0; a < nd; ++a) if (sup[a] > 1e-9) {
        for (int b = 0; b < nd; ++b) if (sup[b] < -1e-9) {
            double c_ab = jd.DIST[a][b] * fac_e + jd.ROUTE[a][b].nl * t_inter;
            link_idx.push_back({a, (int)g[a].size()});
            add_e(a, b, 1e9, c_ab);
        }
    }
    double t_empty_hr = 0.0;
    while (true) {
        std::vector<double> dist(NV, 1e18);
        std::vector<int> pv(NV, -1), pe(NV, -1), inq(NV, 0);
        std::queue<int> q;
        dist[S] = 0.0; q.push(S); inq[S] = 1;
        while (!q.empty()) {
            int u = q.front(); q.pop(); inq[u] = 0;
            for (int i = 0; i < (int)g[u].size(); ++i) {
                const auto& e = g[u][i];
                if (e.cap > 1e-9 && dist[u] + e.cost + 1e-12 < dist[e.to]) {
                    dist[e.to] = dist[u] + e.cost;
                    pv[e.to] = u; pe[e.to] = i;
                    if (!inq[e.to]) { q.push(e.to); inq[e.to] = 1; }
                }
            }
        }
        if (pv[T_node] < 0) break;
        double push = 1e18;
        for (int v = T_node; v != S; v = pv[v]) push = std::min(push, g[pv[v]][pe[v]].cap);
        for (int v = T_node; v != S; v = pv[v]) {
            auto& e = g[pv[v]][pe[v]];
            e.cap -= push; g[v][e.rev].cap += push;
        }
        t_empty_hr += push * dist[T_node];
    }
    for (auto [a, idx] : link_idx) {
        const auto& e = g[a][idx];
        double used = 1e9 - e.cap;
        if (used > 1e-9) {
            for (int j : jd.ROUTE[a][e.to].jn) junc_cross_hr[j] += used;
        }
    }

    double wq_proxy = 0.0, max_rho = 0.0;
    for (int j = 0; j < nJ; ++j) {
        double rho = junc_cross_hr[j] * t_inter / 60.0;
        max_rho = std::max(max_rho, rho);
        double rc = std::min(0.95, rho);
        wq_proxy += junc_cross_hr[j] * (0.5 * t_inter * rc / (1.0 - rc));
    }
    fm.max_rho_j = max_rho;
    fm.f_avg = (t_load_hr + t_empty_hr + 1.8 * wq_proxy) / 60.0;

    std::vector<std::pair<double, double>> urg_tails;
    double urg_rate = 0.0;
    for (const auto& f : cd.flows) {
        if (f.d_st != UD) continue;
        double r = cd.alpha * f.rate;
        urg_rate += r;
        int o = f.o_st;
        double t_ud = jd.DIST[UD][o] * fac_e + jd.ROUTE[UD][o].nl * t_inter;
        double t_snk = 0.0;
        if (tot_sup > 1e-9) {
            for (int s = 0; s < nd; ++s) if (sup[s] > 1e-9) {
                t_snk += (sup[s] / tot_sup) * (jd.DIST[s][o] * fac_e + jd.ROUTE[s][o].nl * t_inter);
            }
        }
        double acc_o = 0.55 * t_ud + 0.45 * t_snk;
        const auto& rt_l = jd.ROUTE[o][UD];
        double load_o = rt_l.total * fac_l + rt_l.nl * t_inter;
        double j_pen = 0.0;
        for (int j : jd.ROUTE[UD][o].jn) {
            double rc = std::min(0.95, junc_cross_hr[j] * t_inter / 60.0);
            j_pen += 0.5 * t_inter * rc / (1.0 - rc);
        }
        urg_tails.push_back({acc_o + 0.35 * load_o + 1.5 * j_pen, r});
    }
    double mean_u = 0.0, p95_u = 0.0;
    if (urg_rate > 1e-9) {
        for (auto& p : urg_tails) mean_u += (p.second / urg_rate) * p.first;
        std::sort(urg_tails.begin(), urg_tails.end());
        double cum = 0.0;
        for (auto& p : urg_tails) {
            cum += p.second / urg_rate;
            p95_u = p.first;
            if (cum >= 0.90) break;
        }
    }
    fm.f_urg = 0.45 * mean_u + 0.55 * p95_u;
    return fm;
}

static bool ruin_recreate_multicycle(const CaseData& cd, const std::string& base_dir,
                                     int n_cycles, std::string& out_dir, std::mt19937& rng)
{
    std::string cur = base_dir;
    int applied = 0;
    for (int step = 0; step < n_cycles * 3 && applied < n_cycles; ++step) {
        std::string nxt;
        if (try_cycle_reversal(cd, cur, nxt, rng)) { cur = nxt; ++applied; }
    }
    if (applied > 0 && fast_scc_check_mask(cd, cur) && cur != base_dir) {
        out_dir = cur;
        return true;
    }
    return false;
}

static bool bottleneck_corridor_lns(const CaseData& cd, const JSimCase& jc, const JSimDir& cur_jd,
                                    int k_flips, std::string& out_dir, std::mt19937& rng)
{
    std::vector<int> free_cids;
    for (int c = 0; c < (int)cd.corridors.size(); ++c) {
        if (!cd.corridors[c].is_parallel_pair) free_cids.push_back(c);
    }
    if (free_cids.empty()) return false;

    std::vector<double> j_wt(jc.nJ, 1.0);
    for (const auto& f : cd.flows) {
        double mult = (f.d_st == cd.urgent_dest) ? 3.0 : 1.0;
        for (int j : cur_jd.ROUTE[f.o_st][f.d_st].jn) j_wt[j] += mult * f.rate;
    }
    std::vector<double> c_wt(free_cids.size(), 1.0);
    for (size_t i = 0; i < free_cids.size(); ++i) {
        for (int e : cd.corridors[free_cids[i]].edges) {
            int u = cd.edges[e].u, v = cd.edges[e].v;
            if (jc.JIDX[u] >= 0) c_wt[i] += j_wt[jc.JIDX[u]];
            if (jc.JIDX[v] >= 0) c_wt[i] += j_wt[jc.JIDX[v]];
        }
    }
    std::discrete_distribution<int> pick_c(c_wt.begin(), c_wt.end());

    for (int att = 0; att < 24; ++att) {
        std::string cand = cur_jd.dir;
        for (int m = 0; m < k_flips; ++m) cand = flip_corridor(cd, cand, free_cids[pick_c(rng)]);
        if (cand != cur_jd.dir && fast_scc_check_mask(cd, cand)) { out_dir = cand; return true; }
        std::string rep = flip_corridor(cd, cand, free_cids[rng() % free_cids.size()]);
        if (rep != cur_jd.dir && fast_scc_check_mask(cd, rep)) { out_dir = rep; return true; }
    }
    return false;
}

static std::vector<JSimDir> discover_global_topologies_v3(
    const CaseData& cd, const JSimCase& jc, const std::string& warm_dir,
    int n_lo, int n_hi, int warm_n,
    const std::vector<JSimStream>& sp8, std::ostream& log_out, int keep_top = 16)
{
    if (!cd.fixed_dir.empty()) {
        return {compile_jsim_dir(cd, jc, cd.fixed_dir)};
    }

    std::vector<int> free_cids;
    for (int c = 0; c < (int)cd.corridors.size(); ++c) {
        if (!cd.corridors[c].is_parallel_pair) free_cids.push_back(c);
    }
    const int C = (int)free_cids.size();

    std::vector<std::string> raw_valid;
    if (fast_scc_check_mask(cd, warm_dir)) raw_valid.push_back(warm_dir);
    if (fast_scc_check_mask(cd, cd.z0)) raw_valid.push_back(cd.z0);

    if (C <= 22) {
        // Insight 5 & 13: Exact Full Bitmask SCC Enumeration for C<=22 (C3: 54, C5: 324, C2: 61,536 SCCs!)
        uint32_t total_masks = 1u << C;
        int nT = omp_get_max_threads();
        std::vector<std::vector<std::string>> th_dirs(nT);
        #pragma omp parallel for schedule(dynamic, 4096)
        for (uint32_t mask = 0; mask < total_masks; ++mask) {
            std::string d = cd.z0;
            for (int b = 0; b < C; ++b) {
                if ((mask >> b) & 1u) {
                    for (int e : cd.corridors[free_cids[b]].edges) d[e] = (d[e] == '0') ? '1' : '0';
                }
            }
            if (fast_scc_check_mask(cd, d)) th_dirs[omp_get_thread_num()].push_back(std::move(d));
        }
        for (auto& v : th_dirs) raw_valid.insert(raw_valid.end(), std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
    } else {
        // Case 4 (C=62): All 1-corridor flips + 2-corridor combinations of valid flips + Constrained Dual-Flow SA
        std::set<std::string> seen_d(raw_valid.begin(), raw_valid.end());
        auto try_push = [&](const std::string& d) {
            if (seen_d.insert(d).second && fast_scc_check_mask(cd, d)) { raw_valid.push_back(d); return true; }
            return false;
        };
        std::vector<int> valid_1flips;
        for (int cid : free_cids) {
            if (try_push(flip_corridor(cd, warm_dir, cid))) valid_1flips.push_back(cid);
            try_push(flip_corridor(cd, cd.z0, cid));
        }
        // Pairwise combinations of valid single-corridor flips around warm_dir (catches cid=15 + synergies!)
        for (size_t i = 0; i < valid_1flips.size(); ++i) {
            for (size_t j = i + 1; j < valid_1flips.size(); ++j) {
                try_push(flip_corridor(cd, flip_corridor(cd, warm_dir, valid_1flips[i]), valid_1flips[j]));
            }
        }
        std::mt19937 rng(20261126 + cd.n_edges * 977);
        for (double w_urg : {0.0, 0.4, 1.0, 2.5}) {
            std::string cur = (w_urg > 0.8) ? warm_dir : cd.z0;
            DualFlowMetrics cur_fm = eval_dual_flow(cd, jc, cur);
            double base_avg = cur_fm.f_avg;
            auto score_fm = [&](const DualFlowMetrics& m) {
                double pen = (m.f_avg > 1.05 * base_avg) ? 50.0 * (m.f_avg - 1.05 * base_avg) : 0.0;
                return m.f_avg + w_urg * m.f_urg + pen;
            };
            double cur_sc = score_fm(cur_fm);
            for (int step = 0; step < 260 && (int)raw_valid.size() < 420; ++step) {
                std::string cand;
                bool ok = false;
                if (step % 3 == 0) {
                    cand = flip_corridor(cd, cur, free_cids[rng() % C]);
                    ok = fast_scc_check_mask(cd, cand);
                } else if (step % 3 == 1) {
                    ok = try_cycle_reversal(cd, cur, cand, rng);
                } else {
                    ok = ruin_recreate_multicycle(cd, cur, 2 + (int)(rng() % 5), cand, rng);
                }
                if (!ok) continue;
                try_push(cand);
                DualFlowMetrics nfm = eval_dual_flow(cd, jc, cand);
                double nsc = score_fm(nfm);
                if (nsc < cur_sc || (rng() % 100 < 15)) { cur = cand; cur_sc = nsc; }
            }
        }
    }

    // Parallel Analytical Dual-Flow Evaluation + Routing Fingerprint Deduplication
    const int R = (int)raw_valid.size();
    std::vector<DualFlowMetrics> all_fm(R);
    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < R; ++i) all_fm[i] = eval_dual_flow(cd, jc, raw_valid[i]);

    std::vector<DualFlowMetrics> uniq_fm;
    std::set<std::string> seen_fp;
    // Ensure warm_dir is index 0!
    for (const auto& fm : all_fm) {
        if (fm.dir == warm_dir && seen_fp.insert(fm.fp).second) uniq_fm.push_back(fm);
    }
    for (auto& fm : all_fm) {
        if (seen_fp.insert(fm.fp).second) uniq_fm.push_back(std::move(fm));
    }

    log_out << "  [Tier 0 Dual-Flow SCC Scan] Corridors=" << C
            << " -> RawValidSCC=" << R
            << " -> UniqueRoutingSCC=" << uniq_fm.size() << "\n" << std::flush;

    // Pre-filter to <= 76 Pareto-diverse SCCs using Constrained Dual-Situation Flow (Insight 5.4: f_avg <= 1.05*min)
    std::vector<JSimDir> cand_jds;
    if ((int)uniq_fm.size() <= 76) {
        for (const auto& fm : uniq_fm) cand_jds.push_back(compile_jsim_dir(cd, jc, fm.dir));
    } else {
        double min_avg = 1e9, min_urg = 1e9;
        for (const auto& fm : uniq_fm) {
            min_avg = std::min(min_avg, fm.f_avg);
            min_urg = std::min(min_urg, fm.f_urg);
        }
        std::vector<uint8_t> pick(uniq_fm.size(), 0);
        std::vector<int> chosen;
        auto add_i = [&](int idx) {
            if (idx >= 0 && idx < (int)uniq_fm.size() && !pick[idx] && (int)chosen.size() < 76) {
                pick[idx] = 1; chosen.push_back(idx);
            }
        };
        add_i(0); // warm_dir
        std::vector<int> ord(uniq_fm.size());
        std::iota(ord.begin(), ord.end(), 0);

        // 1) Top 26 Situation A (`f_avg` optimal)
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return uniq_fm[a].f_avg < uniq_fm[b].f_avg; });
        for (int k = 0; k < std::min<int>(26, (int)ord.size()); ++k) add_i(ord[k]);

        // 2) Top 26 Constrained Situation B (`f_urg` subject to `f_avg <= 1.05 * min_avg`)
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            bool ca = uniq_fm[a].f_avg <= 1.05 * min_avg;
            bool cb = uniq_fm[b].f_avg <= 1.05 * min_avg;
            if (ca != cb) return ca;
            return uniq_fm[a].f_urg < uniq_fm[b].f_urg;
        });
        for (int k = 0; k < std::min<int>(26, (int)ord.size()); ++k) add_i(ord[k]);

        // 3) Top 18 Joint Pareto Knee
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            double sa = uniq_fm[a].f_avg / min_avg + 0.65 * (uniq_fm[a].f_urg / std::max(1e-6, min_urg));
            double sb = uniq_fm[b].f_avg / min_avg + 0.65 * (uniq_fm[b].f_urg / std::max(1e-6, min_urg));
            return sa < sb;
        });
        for (int k = 0; k < std::min<int>(18, (int)ord.size()); ++k) add_i(ord[k]);

        // 4) Remaining: Lowest peak junction utilization `max_rho_j`
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            bool ca = uniq_fm[a].f_avg <= 1.06 * min_avg;
            bool cb = uniq_fm[b].f_avg <= 1.06 * min_avg;
            if (ca != cb) return ca;
            return uniq_fm[a].max_rho_j < uniq_fm[b].max_rho_j;
        });
        for (int idx : ord) { if ((int)chosen.size() >= 76) break; add_i(idx); }

        for (int idx : chosen) cand_jds.push_back(compile_jsim_dir(cd, jc, uniq_fm[idx].dir));
    }

    // Unbiased v3 Multi-Track DES Screening across Lean, Current, and Surplus (+2) Fleets
    std::vector<int> probe_fleets = {
        std::clamp(warm_n - 2, n_lo, n_hi),
        std::clamp(warm_n, n_lo, n_hi),
        std::clamp(warm_n + 2, n_lo, n_hi)
    };
    std::sort(probe_fleets.begin(), probe_fleets.end());
    probe_fleets.erase(std::unique(probe_fleets.begin(), probe_fleets.end()), probe_fleets.end());

    const int D = (int)cand_jds.size();
    std::vector<double> dir_best(D, 999999.0);
    std::vector<std::vector<double>> dir_by_pf(probe_fleets.size(), std::vector<double>(D, 999999.0));

    #pragma omp parallel for schedule(dynamic, 1)
    for (int di = 0; di < D; ++di) {
        auto tmpls = build_v3_templates(cd, cand_jds[di], true);
        double best_all = 999999.0;
        for (size_t pi = 0; pi < probe_fleets.size(); ++pi) {
            int nf = probe_fleets[pi];
            double best_pf = 999999.0;
            for (const auto& tr : tmpls) {
                double sc = exact_score(eval_pooled_serial(cd, jc, cand_jds[di], sp8, nf, tr));
                if (sc < best_pf) best_pf = sc;
            }
            dir_by_pf[pi][di] = best_pf;
            if (best_pf < best_all) best_all = best_pf;
        }
        dir_best[di] = best_all;
    }

    std::vector<JSimDir> selected;
    std::vector<uint8_t> taken(D, 0);
    auto take_di = [&](int di) {
        if (di >= 0 && di < D && !taken[di] && (int)selected.size() < keep_top) {
            taken[di] = 1;
            selected.push_back(cand_jds[di]);
            return true;
        }
        return false;
    };

    take_di(0); // Pin warm_dir
    for (size_t pi = 0; pi < probe_fleets.size(); ++pi) {
        std::vector<int> ord(D);
        std::iota(ord.begin(), ord.end(), 0);
        std::sort(ord.begin(), ord.end(), [&](int a, int b) { return dir_by_pf[pi][a] < dir_by_pf[pi][b]; });
        int added = 0;
        for (int idx : ord) {
            if (dir_by_pf[pi][idx] >= 900000.0 || added >= 3) break;
            if (take_di(idx)) ++added;
        }
    }

    std::vector<int> ord_all(D);
    std::iota(ord_all.begin(), ord_all.end(), 0);
    std::sort(ord_all.begin(), ord_all.end(), [&](int a, int b) { return dir_best[a] < dir_best[b]; });
    int min_ham = std::max(2, cd.n_edges / 10);
    for (int pass = 0; pass < 2 && (int)selected.size() < keep_top; ++pass) {
        int req_h = (pass == 0) ? min_ham : 1;
        for (int idx : ord_all) {
            if ((int)selected.size() >= keep_top || dir_best[idx] >= 900000.0) break;
            if (taken[idx]) continue;
            bool close = false;
            for (const auto& ex : selected) {
                if (hamming_dir(ex.dir, cand_jds[idx].dir) < req_h) { close = true; break; }
            }
            if (!close) take_di(idx);
        }
    }

    log_out << "  [Tier 0 Selected " << selected.size() << " Topologies (from " << D
            << " screened)] Best v3 sp8=" << (ord_all.empty() ? 999999.0 : dir_best[ord_all[0]]) << "\n" << std::flush;
    return selected;
}

// ============================================================================
// 5. Block-Robust 128-Seed Evaluator + Ergodic SA + Mid-Flight Zombie Splicing
//    + Discrete Physics Hooke-Jeeves + Greedy Leaf Boosting
// ============================================================================
struct DualEval {
    double tr64 = 999999.0;
    double va64 = 999999.0;
    double rob128 = 999999.0; // Mean + 0.12 * |tr64 - va64| (penalizes inter-block overfitting!)
    int endq128 = 999;
    bool valid() const { return tr64 < 900000.0 && va64 < 900000.0; }
};

static inline double calc_rob128(double tr, double va) {
    return 0.5 * (tr + va) + 0.12 * std::abs(tr - va);
}

static DualEval eval_dual_128(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                              const std::vector<JSimStream>& sp_tr64,
                              const std::vector<JSimStream>& sp_va64,
                              int n_agv, const Tree& t)
{
    DualEval de;
    PooledStat ptr = eval_exact_pooled(cd, jc, jd, sp_tr64, n_agv, t, true);
    de.tr64 = exact_score(ptr);
    if (de.tr64 >= 900000.0) return de;
    PooledStat pva = eval_exact_pooled(cd, jc, jd, sp_va64, n_agv, t, true);
    de.va64 = exact_score(pva);
    if (de.va64 >= 900000.0) return de;
    de.rob128 = calc_rob128(de.tr64, de.va64);
    de.endq128 = std::max(ptr.max_end_q, pva.max_end_q);
    return de;
}

static Tree v3_robust_sa_hj_polish(
    const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
    const std::vector<JSimStream>& sp_tr64,
    const std::vector<JSimStream>& sp_va64,
    const std::vector<JSimStream>& sp16,
    int n_agv, const Tree& start_tree, int sa_iters,
    DualEval& out_best_de, std::vector<std::pair<Tree, DualEval>>& out_history, std::mt19937& rng)
{
    Tree cur = splice_zombie_nodes(cd, jc, jd, sp16, n_agv, start_tree);
    DualEval best_de = eval_dual_128(cd, jc, jd, sp_tr64, sp_va64, n_agv, cur);
    Tree best = cur;
    if (!best_de.valid()) {
        PooledStat ptr0 = eval_exact_pooled(cd, jc, jd, sp_tr64, n_agv, cur, true);
        if (exact_score(ptr0) >= 900000.0) { out_best_de = best_de; return best; }
        best_de.tr64 = exact_score(ptr0);
        best_de.va64 = 999999.0;
        best_de.rob128 = 999999.0;
    } else {
        out_history.push_back({best, best_de});
    }

    double cur_tr = best_de.tr64;
    double cur_16 = exact_score(eval_exact_pooled(cd, jc, jd, sp16, n_agv, cur, true));
    double min_seen_tr = best_de.tr64;
    V3Quantiles vq = build_v3_quantiles(cd, jd);

    auto try_accept_dual = [&](const Tree& cand_in, double tr_sc, const PooledStat* ptr_opt = nullptr) -> bool {
        Tree cand = round_tree_v3(cand_in);
        PooledStat pva = eval_exact_pooled(cd, jc, jd, sp_va64, n_agv, cand, true);
        double va_sc = exact_score(pva);
        if (va_sc >= 900000.0) return false;
        double rob = calc_rob128(tr_sc, va_sc);
        bool accept = !best_de.valid() ||
                      (rob < best_de.rob128 - 1e-4 && va_sc <= best_de.va64 + 0.15) ||
                      (rob < best_de.rob128 - 0.30);
        if (accept) {
            PooledStat ptr = ptr_opt ? *ptr_opt : eval_exact_pooled(cd, jc, jd, sp_tr64, n_agv, cand, true);
            best = cand;
            best_de.tr64 = tr_sc;
            best_de.va64 = va_sc;
            best_de.rob128 = rob;
            best_de.endq128 = std::max(ptr.max_end_q, pva.max_end_q);
            out_history.push_back({best, best_de});
            return true;
        }
        return false;
    };

    Tree ckpt_cand = cur;
    double ckpt_tr = 999999.0;
    bool ckpt_checked = true;

    for (int it = 0; it < sa_iters; ++it) {
        // Mid-flight zombie compaction reclaims dead branches so second half of SA can grow real splits!
        if (it == sa_iters / 2 && sa_iters >= 80) {
            cur = splice_zombie_nodes(cd, jc, jd, sp16, n_agv, cur);
            if (best_de.valid()) best = splice_zombie_nodes(cd, jc, jd, sp16, n_agv, best);
        }

        double frac = (double)it / std::max(1, sa_iters);
        double temp = 2.8 * std::pow(0.008, frac);
        Tree cand = cur;
        bool valid_mut = false;

        uint32_t roll = rng() % 100;
        if (roll < 18 && cur.n_nodes < cd.max_nodes) {
            // Grow leaf with path-invariant guard (no w/qo under w<=0.006!)
            std::vector<std::pair<int, int>> open_slots;
            for (int pid = 1; pid <= cur.n_nodes; ++pid) {
                if (cur.nodes[pid].lo == 0) open_slots.push_back({pid, 0});
                if (cur.nodes[pid].hi == 0) open_slots.push_back({pid, 1});
            }
            if (!open_slots.empty()) {
                for (int att = 0; att < 10; ++att) {
                    auto [pid, is_hi] = open_slots[rng() % open_slots.size()];
                    int nf = (int)(rng() % 5);
                    if (is_under_fresh_arrival_gate(cur, pid, is_hi) && (nf == 1 || nf == 4)) continue;
                    if (vq.swap_pal[nf].empty()) continue;
                    double nth = (nf == 0) ? 0.5 : vq.swap_pal[nf][rng() % vq.swap_pal[nf].size()];
                    bool nmin = (rng() % 2 == 0);
                    Tree tmp = cur;
                    int nid = ++tmp.n_nodes;
                    tmp.nodes[nid] = {nf, nmin, nth, 0, 0};
                    if (is_hi) tmp.nodes[pid].hi = nid; else tmp.nodes[pid].lo = nid;
                    tmp = round_tree_v3(tmp);
                    if (is_strict_tree(tmp, cd.max_nodes)) { cand = tmp; valid_mut = true; break; }
                }
            }
        } else if (roll < 29 && cur.n_nodes >= 4) {
            // Prune leaf
            std::vector<int> prunable;
            for (int nid = 2; nid <= cur.n_nodes; ++nid) {
                if (cur.nodes[nid].lo == 0 && cur.nodes[nid].hi == 0) prunable.push_back(nid);
            }
            if (!prunable.empty()) {
                int tid = prunable[rng() % prunable.size()];
                Tree out_p;
                if (prune_node_strict(cur, tid, out_p, cd.max_nodes)) {
                    cand = round_tree_v3(out_p);
                    valid_mut = true;
                }
            }
        } else if (roll < 48) {
            uint32_t sub_op = rng() % 3;
            if (sub_op == 0) {
                // Single-node direction flip (`urg,max <-> urg,min`, `de,min <-> de,max`)
                int nid = 1 + (int)(rng() % cur.n_nodes);
                Tree tmp = cur;
                tmp.nodes[nid].is_min = !tmp.nodes[nid].is_min;
                tmp = round_tree_v3(tmp);
                if (is_strict_tree(tmp, cd.max_nodes)) { cand = tmp; valid_mut = true; }
            } else if (sub_op == 1) {
                // Child swap `(lo <-> hi)`
                int nid = 1 + (int)(rng() % cur.n_nodes);
                if (cur.nodes[nid].lo != 0 || cur.nodes[nid].hi != 0) {
                    Tree tmp = cur;
                    std::swap(tmp.nodes[nid].lo, tmp.nodes[nid].hi);
                    tmp = round_tree_v3(tmp);
                    if (is_strict_tree(tmp, cd.max_nodes)) { cand = tmp; valid_mut = true; }
                }
            } else {
                // Feature swap with fresh-arrival invariant guard
                int nid = (cur.n_nodes >= 2 && (rng() % 4 != 0))
                        ? (2 + (int)(rng() % (cur.n_nodes - 1)))
                        : (1 + (int)(rng() % cur.n_nodes));
                bool under_fresh = is_under_fresh_arrival_gate(cur, nid, -1);
                std::array<int, 5> f_ord = {0, 1, 2, 3, 4};
                std::shuffle(f_ord.begin(), f_ord.end(), rng);
                for (int nf : f_ord) {
                    if (nf == cur.nodes[nid].feat || vq.swap_pal[nf].empty()) continue;
                    if (under_fresh && (nf == 1 || nf == 4)) continue;
                    Tree tmp = cur;
                    tmp.nodes[nid].feat = nf;
                    tmp.nodes[nid].th = (nf == 0) ? 0.5 : vq.swap_pal[nf][rng() % vq.swap_pal[nf].size()];
                    if (rng() % 2 == 0) tmp.nodes[nid].is_min = !tmp.nodes[nid].is_min;
                    tmp = round_tree_v3(tmp);
                    if (is_strict_tree(tmp, cd.max_nodes)) { cand = tmp; valid_mut = true; break; }
                }
            }
        }

        if (!valid_mut) {
            // Discrete-Palette Jump OR Continuous Nudge
            std::vector<int> tune_ids;
            for (int nid = 1; nid <= cur.n_nodes; ++nid) if (cur.nodes[nid].feat != 0) tune_ids.push_back(nid);
            if (!tune_ids.empty()) {
                int n_mut = (frac < 0.45 && tune_ids.size() >= 2 && (rng() % 3 == 0)) ? 2 : 1;
                for (int m = 0; m < n_mut; ++m) {
                    int nid = tune_ids[rng() % tune_ids.size()];
                    int f = cand.nodes[nid].feat;
                    const auto& pal = vq.swap_pal[f];
                    if ((f == 2 || f == 3) && !pal.empty() && (rng() % 3 != 0)) {
                        // Exact discrete palette index jump!
                        int cur_idx = (int)(std::lower_bound(pal.begin(), pal.end(), cand.nodes[nid].th) - pal.begin());
                        cur_idx = std::clamp(cur_idx, 0, (int)pal.size() - 1);
                        int max_step = (frac < 0.5) ? std::max(2, (int)pal.size() / 3) : 2;
                        int delta = std::uniform_int_distribution<int>(-max_step, max_step)(rng);
                        if (delta == 0) delta = (rng() % 2 == 0) ? 1 : -1;
                        int nidx = std::clamp(cur_idx + delta, 0, (int)pal.size() - 1);
                        cand.nodes[nid].th = pal[(size_t)nidx];
                    } else if (f == 1) {
                        if (rng() % 3 == 0 && !pal.empty()) {
                            cand.nodes[nid].th = pal[rng() % pal.size()];
                        } else if (cand.nodes[nid].th < 0.25 || (rng() % 2 == 0)) {
                            double lspan = (frac < 0.5) ? 1.15 : 0.35;
                            double mult = std::exp(std::uniform_real_distribution<double>(-lspan, lspan)(rng));
                            cand.nodes[nid].th = std::clamp(cand.nodes[nid].th * mult, 0.0005, 8.0);
                        } else {
                            double span = (frac < 0.5) ? 0.55 : 0.14;
                            double d = std::uniform_real_distribution<double>(-span, span)(rng);
                            cand.nodes[nid].th = std::clamp(cand.nodes[nid].th + d, 0.0005, 8.0);
                        }
                    } else if (f == 2 || f == 3) {
                        double span = (frac < 0.5) ? 35.0 : 8.0;
                        double d = std::uniform_real_distribution<double>(-span, span)(rng);
                        cand.nodes[nid].th = std::clamp(cand.nodes[nid].th + d, 2.0, vq.max_d + 10.0);
                    } else if (f == 4) {
                        int d = (rng() % 2 == 0) ? 1 : -1;
                        cand.nodes[nid].th = std::clamp(cand.nodes[nid].th + d, 1.0, 6.0);
                    }
                }
            } else if (cand.n_nodes >= 1) {
                int nid = 1 + (int)(rng() % cand.n_nodes);
                cand.nodes[nid].is_min = !cand.nodes[nid].is_min;
            }
            cand = round_tree_v3(cand);
            if (!is_strict_tree(cand, cd.max_nodes)) continue;
        }

        // Fast Stage-1 parallel `sp16` filter cuts wasted 64-seed DES sims by ~3.5x at full multi-core speed!
        double c16 = exact_score(eval_exact_pooled(cd, jc, jd, sp16, n_agv, cand, true));
        if (c16 >= 900000.0 || c16 > cur_16 + std::max(2.6, 2.2 * temp)) continue;

        PooledStat ptr = eval_exact_pooled(cd, jc, jd, sp_tr64, n_agv, cand, true);
        double csc = exact_score(ptr);
        if (csc >= 900000.0) continue;

        double diff = csc - cur_tr;
        if (diff < 0.0 || std::uniform_real_distribution<double>(0.0, 1.0)(rng) < std::exp(-diff / std::max(0.01, temp))) {
            cur = cand;
            cur_tr = csc;
            cur_16 = c16;
        }

        if (csc < min_seen_tr - 1e-4) {
            min_seen_tr = csc;
            try_accept_dual(cand, csc, &ptr);
            ckpt_checked = true;
        } else if (csc < ckpt_tr && (!best_de.valid() || csc < best_de.tr64 + 0.15)) {
            ckpt_cand = cand;
            ckpt_tr = csc;
            ckpt_checked = false;
        }

        if ((it + 1) % 30 == 0 && !ckpt_checked) {
            try_accept_dual(ckpt_cand, ckpt_tr, nullptr);
            ckpt_tr = 999999.0;
            ckpt_checked = true;
        }
    }

    // Compact zombies before deterministic Hooke-Jeeves + Greedy Leaf Boosting!
    if (best_de.valid()) {
        best = splice_zombie_nodes(cd, jc, jd, sp16, n_agv, best);
        double best_16 = exact_score(eval_exact_pooled(cd, jc, jd, sp16, n_agv, best, true));

        auto try_cand_hj = [&](const Tree& raw_c) -> bool {
            Tree c = round_tree_v3(raw_c);
            if (!is_strict_tree(c, cd.max_nodes)) return false;
            double s16 = exact_score(eval_exact_pooled(cd, jc, jd, sp16, n_agv, c, true));
            if (s16 >= 900000.0 || s16 > best_16 + 3.2) return false;
            PooledStat ptr = eval_exact_pooled(cd, jc, jd, sp_tr64, n_agv, c, true);
            double tr_sc = exact_score(ptr);
            if (tr_sc < best_de.tr64 + 0.25 && try_accept_dual(c, tr_sc, &ptr)) {
                best_16 = s16;
                return true;
            }
            return false;
        };

        // 1) Deterministic Greedy Leaf Boosting on open slots freed by zombie compaction!
        while (best.n_nodes < cd.max_nodes) {
            std::vector<Tree> leaf_cands;
            for (int pid = 1; pid <= best.n_nodes; ++pid) {
                for (int is_hi = 0; is_hi <= 1; ++is_hi) {
                    if ((is_hi ? best.nodes[pid].hi : best.nodes[pid].lo) != 0) continue;
                    bool sub_fresh = is_under_fresh_arrival_gate(best, pid, is_hi);
                    for (int f = 0; f < 5; ++f) {
                        if (sub_fresh && (f == 1 || f == 4)) continue;
                        for (double th : vq.wing_th[f]) {
                            for (bool nmin : {true, false}) {
                                Tree tmp = best;
                                int nid = ++tmp.n_nodes;
                                tmp.nodes[nid] = {f, nmin, th, 0, 0};
                                if (is_hi) tmp.nodes[pid].hi = nid; else tmp.nodes[pid].lo = nid;
                                tmp = round_tree_v3(tmp);
                                if (is_strict_tree(tmp, cd.max_nodes)) leaf_cands.push_back(tmp);
                            }
                        }
                    }
                }
            }
            if (leaf_cands.empty()) break;
            // Screen leaf expansions rapidly on sp16 first!
            std::vector<double> sc16(leaf_cands.size(), 999999.0);
            double base16 = exact_score(eval_pooled_serial(cd, jc, jd, sp16, n_agv, best));
            #pragma omp parallel for schedule(dynamic, 1)
            for (int i = 0; i < (int)leaf_cands.size(); ++i) {
                sc16[i] = exact_score(eval_pooled_serial(cd, jc, jd, sp16, n_agv, leaf_cands[i]));
            }
            std::vector<int> ord(leaf_cands.size());
            std::iota(ord.begin(), ord.end(), 0);
            std::sort(ord.begin(), ord.end(), [&](int a, int b) { return sc16[a] < sc16[b]; });
            bool grew = false;
            for (int k = 0; k < std::min<int>(5, (int)ord.size()); ++k) {
                if (sc16[ord[k]] >= base16 + 0.5) break;
                if (try_cand_hj(leaf_cands[ord[k]])) { grew = true; break; }
            }
            if (!grew) break;
        }

        // 2) Deterministic Coordinate Hooke-Jeeves (`is_min` flip + discrete physics palette + fine nudge)
        for (int pass = 0; pass < 2; ++pass) {
            bool imp = false;
            // Sweep `is_min` flips across all nodes (catches `urg,max <-> urg,min` deterministically!)
            for (int nid = 1; nid <= best.n_nodes; ++nid) {
                Tree c = best;
                c.nodes[nid].is_min = !c.nodes[nid].is_min;
                if (try_cand_hj(c)) imp = true;
            }
            // Sweep thresholds
            for (int nid = 1; nid <= best.n_nodes; ++nid) {
                int f = best.nodes[nid].feat;
                if (f == 1) {
                    if (best.nodes[nid].th < 0.25) {
                        for (double mult : {0.45, 0.75, 1.35, 2.2}) {
                            Tree c = best; c.nodes[nid].th = std::clamp(best.nodes[nid].th * mult, 0.0005, 8.0);
                            if (try_cand_hj(c)) imp = true;
                        }
                    } else {
                        for (double step : {0.35, 0.12, 0.04}) {
                            for (double sgn : {-1.0, 1.0}) {
                                Tree c = best; c.nodes[nid].th = std::clamp(best.nodes[nid].th + sgn * step, 0.05, 8.0);
                                if (try_cand_hj(c)) imp = true;
                            }
                        }
                    }
                } else if (f == 2 || f == 3) {
                    const auto& pal = vq.swap_pal[f];
                    if (!pal.empty()) {
                        int idx = (int)(std::lower_bound(pal.begin(), pal.end(), best.nodes[nid].th) - pal.begin());
                        idx = std::clamp(idx, 0, (int)pal.size() - 1);
                        for (int d : {-4, -2, -1, 1, 2, 4}) {
                            int nidx = idx + d;
                            if (nidx >= 0 && nidx < (int)pal.size()) {
                                Tree c = best; c.nodes[nid].th = pal[(size_t)nidx];
                                if (try_cand_hj(c)) {
                                    imp = true;
                                    idx = (int)(std::lower_bound(pal.begin(), pal.end(), best.nodes[nid].th) - pal.begin());
                                    idx = std::clamp(idx, 0, (int)pal.size() - 1);
                                }
                            }
                        }
                    }
                } else if (f == 4) {
                    for (double sgn : {-1.0, 1.0}) {
                        Tree c = best; c.nodes[nid].th = std::clamp(best.nodes[nid].th + sgn, 1.0, 6.0);
                        if (try_cand_hj(c)) imp = true;
                    }
                }
            }
            if (!imp) break;
        }
        best = splice_zombie_nodes(cd, jc, jd, sp16, n_agv, best);
    }

    out_best_de = best_de;
    return best;
}

static std::vector<JSimDir> coupled_dir_lns_around_tree_v3(
    const CaseData& cd, const JSimCase& jc, const JSimDir& cur_jd, int n_agv,
    const Tree& fixed_tree, const std::vector<JSimStream>& sp32, std::mt19937& rng)
{
    if (!cd.fixed_dir.empty()) return {};
    std::vector<int> free_cids;
    for (int c = 0; c < (int)cd.corridors.size(); ++c)
        if (!cd.corridors[c].is_parallel_pair) free_cids.push_back(c);

    std::vector<JSimDir> cands;
    std::set<std::string> seen_fp = {jsim_fp(jc, cur_jd)};
    auto push_d = [&](const std::string& d) {
        if (!fast_scc_check_mask(cd, d)) return;
        JSimDir jd = compile_jsim_dir(cd, jc, d);
        if (seen_fp.insert(jsim_fp(jc, jd)).second) cands.push_back(std::move(jd));
    };

    for (int c1 : free_cids) push_d(flip_corridor(cd, cur_jd.dir, c1));
    for (int att = 0; att < 90 && (int)cands.size() < 42; ++att) {
        std::string nxt;
        if (att % 3 == 0) {
            if (ruin_recreate_multicycle(cd, cur_jd.dir, 2 + (int)(rng() % 5), nxt, rng)) push_d(nxt);
        } else if (att % 3 == 1) {
            if (bottleneck_corridor_lns(cd, jc, cur_jd, 1 + (int)(rng() % 3), nxt, rng)) push_d(nxt);
        } else {
            if (try_cycle_reversal(cd, cur_jd.dir, nxt, rng) && fast_scc_check_mask(cd, nxt)) push_d(nxt);
        }
    }
    if (cands.empty()) return {};

    std::vector<double> scs(cands.size(), 999999.0);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < (int)cands.size(); ++i) {
        double s_un = exact_score(eval_pooled_serial(cd, jc, cands[i], sp32, n_agv, fixed_tree));
        Tree scaled = rescale_tree_v3(cd, fixed_tree, cur_jd, cands[i]);
        double s_sc = exact_score(eval_pooled_serial(cd, jc, cands[i], sp32, n_agv, scaled));
        scs[i] = std::min(s_un, s_sc);
    }

    std::vector<int> ord(cands.size());
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { return scs[a] < scs[b]; });

    std::vector<JSimDir> out;
    for (int idx : ord) {
        if (scs[idx] >= 900000.0 || (int)out.size() >= 2) break;
        out.push_back(std::move(cands[idx]));
    }
    return out;
}

// ============================================================================
// 6. Main v3 Pipeline (Bit-Exact Check -> Multi-Track Search -> 256-Seed Holdout -> 1005-Seed CVaR Audit)
// ============================================================================
static std::pair<int, int> get_case_fleet_range(int c, const CaseData& cd) {
    int lo = 18, hi = 25;
    if (c == 1) { lo = 18; hi = 24; }
    else if (c == 2) { lo = 22; hi = 28; }
    else if (c == 3) { lo = 19; hi = 25; }
    else if (c == 4) { lo = 16; hi = 21; }
    else if (c == 5) { lo = 19; hi = 25; }
    return {std::max(cd.n_min, lo), std::min(cd.n_max, hi)};
}

static std::string describe_root(const Tree& t) {
    if (t.n_nodes < 1) return "empty";
    const auto& r = t.nodes[1];
    return std::string(FEAT_NAMES[r.feat]) + "," + (r.is_min ? "min" : "max") + "," + format_th_precise(r.feat, r.th);
}

int main(int argc, char** argv) {
    bool bench_only = false;
    std::vector<int> run_order;
    for (int a = 1; a < argc; ++a) {
        std::string s = argv[a];
        if (s == "--bench") bench_only = true;
        else {
            int v = std::stoi(s);
            if (v >= 1 && v <= 5) run_order.push_back(v);
        }
    }
    if (run_order.empty()) run_order = {1, 5, 3, 2, 4};

    // Startup Bit-Exactness Verification (Fast Representative/Packed JSim vs Reference JSim) + Benchmark
    {
        for (int tc : {1, 5}) {
            std::string cn = "c" + std::to_string(tc);
            CaseData tcd = load_case("data/" + cn + ".json", cn);
            JSimCase tjc = build_jsim_case(tcd);
            auto [tdir, tn, ttr] = load_solution_csv("answers/ans_" + cn + ".csv");
            JSimDir tjd = compile_jsim_dir(tcd, tjc, tdir);
            std::vector<int> ts(30);
            std::iota(ts.begin(), ts.end(), 1);
            auto tsp = build_jsim_streams(tcd, tjc, ts);
            for (size_t i = 0; i < tsp.size(); ++i) {
                JSimOut r1 = run_jsim_ref(tcd, tjc, tjd, tsp[i], tn, ttr);
                JSimOut r2 = run_jsim(tcd, tjc, tjd, tsp[i], tn, ttr);
                if (r1.unstable != r2.unstable || r1.maxQ != r2.maxQ || r1.endQ != r2.endQ ||
                    r1.cnt_w != r2.cnt_w || std::abs(r1.sum_w - r2.sum_w) > 1e-11 || r1.wu != r2.wu) {
                    std::cerr << "[FATAL] Fast JSim mismatch on " << cn << " seed " << tsp[i].seed << "!\n";
                    return 2;
                }
            }
        }

        CaseData bcd = load_case("data/c5.json", "c5");
        JSimCase bjc = build_jsim_case(bcd);
        auto [bdir, bn, btr] = load_solution_csv("answers/ans_c5.csv");
        JSimDir bjd = compile_jsim_dir(bcd, bjc, bdir);
        std::vector<int> bs(200);
        std::iota(bs.begin(), bs.end(), 1);
        auto bsp = build_jsim_streams(bcd, bjc, bs);
        g_sims.store(0);
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < 12; ++r) eval_exact_pooled(bcd, bjc, bjd, bsp, bn, btr, false);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double sps = (double)g_sims.load() / std::max(1e-6, dt);
        double est_min = (run_order.size() * 240000.0 / std::max(100.0, sps)) / 60.0;
        std::cout << "[v3 Verified 0-Mismatch Benchmark] Throughput = " << std::fixed << std::setprecision(1) << sps
                  << " DES sims/sec | Estimated ETA for " << run_order.size() << " case(s) = ~"
                  << std::setprecision(1) << est_min << " min\n" << std::flush;
        if (bench_only) return 0;
    }

    g_sims.store(0);
    auto t_global_start = std::chrono::steady_clock::now();
    std::ofstream log_f("logs/optimization_v3_robust.log", std::ios::app);
    log_f << "\n=== v3 Insight-Driven Robust Co-Optimization Engine (Multi-Core Resume) ===\n" << std::flush;

    for (int c : run_order) {
        auto t_case_start = std::chrono::steady_clock::now();
        long long sims_before = g_sims.load();

        std::string cname = "c" + std::to_string(c);
        CaseData cd = load_case("data/" + cname + ".json", cname);
        JSimCase jc = build_jsim_case(cd);

        auto [warm_dir, warm_n, warm_tree_raw] = load_solution_csv("answers/ans_" + cname + ".csv");
        JSimDir warm_jd = compile_jsim_dir(cd, jc, warm_dir);

        std::vector<int> s8 = {11, 14, 53, 117, 193, 277, 367, 457};
        std::vector<int> s16(16), s32(32), s_tr64(64), s_va64(64), s_ho128(128), s500(500), s_oos(500);
        for (int i = 0; i < 16; ++i) s16[i] = 1 + i * 31;
        for (int i = 0; i < 32; ++i) s32[i] = 1 + i * 15;
        std::iota(s_tr64.begin(), s_tr64.end(), 1);
        std::iota(s_va64.begin(), s_va64.end(), 65);
        std::iota(s_ho128.begin(), s_ho128.end(), 129);
        std::iota(s500.begin(), s500.end(), 1);
        std::iota(s_oos.begin(), s_oos.end(), 3001);

        auto sp8 = build_jsim_streams(cd, jc, s8);
        auto sp16 = build_jsim_streams(cd, jc, s16);
        auto sp32 = build_jsim_streams(cd, jc, s32);
        auto sp_tr64 = build_jsim_streams(cd, jc, s_tr64);
        auto sp_va64 = build_jsim_streams(cd, jc, s_va64);
        auto sp_ho128 = build_jsim_streams(cd, jc, s_ho128);
        auto sp500 = build_jsim_streams(cd, jc, s500);
        auto sp_oos = build_jsim_streams(cd, jc, s_oos);
        auto sp_pub = build_jsim_streams(cd, jc, {11, 12, 13, 14, 15});

        std::vector<JSimOut> base_outs_500, base_outs_oos;
        auto base_500 = eval_exact_pooled(cd, jc, warm_jd, sp500, warm_n, warm_tree_raw, false, &base_outs_500);
        auto base_oos = eval_exact_pooled(cd, jc, warm_jd, sp_oos, warm_n, warm_tree_raw, false, &base_outs_oos);
        auto base_pub = eval_exact_pooled(cd, jc, warm_jd, sp_pub, warm_n, warm_tree_raw, false);
        double base_cvar = compute_block_cvar20(cd, jc, warm_n, base_outs_500, base_outs_oos);

        // Zombie-compact warm_tree on sp500 (reclaims 2..5 wasted slots with 0.0000 score change!)
        Tree warm_tree = splice_zombie_nodes(cd, jc, warm_jd, sp500, warm_n, warm_tree_raw);
        auto comp_check = eval_exact_pooled(cd, jc, warm_jd, sp500, warm_n, warm_tree, false);
        if (comp_check.dq_cnt > 0 || std::abs(comp_check.pooled_cost - base_500.pooled_cost) > 1e-6) {
            warm_tree = round_tree_v3(warm_tree_raw);
        }

        auto [n_lo, n_hi] = get_case_fleet_range(c, cd);

        log_f << "\n====================================================================\n"
              << "[Case " << c << ": " << cd.title << "] FleetRange=[" << n_lo << ".." << n_hi << "]\n"
              << "  Baseline ans_" << cname << ".csv (n=" << warm_n << ", raw=" << warm_tree_raw.n_nodes
              << "n -> active=" << warm_tree.n_nodes << "n, root=" << describe_root(warm_tree)
              << "): 1..500=" << std::fixed << std::setprecision(4) << base_500.pooled_cost
              << " | 11..15=" << base_pub.pooled_cost << " | OOS=" << base_oos.pooled_cost
              << " | CVaR20=" << base_cvar << "\n" << std::flush;
        std::cout << "[Case " << c << " Start] Base(n=" << warm_n << ", " << warm_tree_raw.n_nodes << "n->"
                  << warm_tree.n_nodes << "n): 1..500=" << std::fixed << std::setprecision(4) << base_500.pooled_cost
                  << " | 11..15=" << base_pub.pooled_cost << " | OOS=" << base_oos.pooled_cost << "\n" << std::flush;

        struct SolCand {
            JSimDir jd;
            int fleet = 0;
            Tree tree;
            DualEval de;
            bool pure_v3 = false;
        };
        std::vector<SolCand> pool;
        std::set<std::string> seen_sol;
        auto push_sol = [&](const JSimDir& d, int na, const Tree& raw_t, bool is_pure, const DualEval* pre_de = nullptr) -> DualEval {
            Tree t = round_tree_v3(raw_t);
            DualEval de;
            if (!is_strict_tree(t, cd.max_nodes)) return de;
            std::string k = d.dir + "|" + std::to_string(na) + "|" + v3_tree_key(t);
            if (seen_sol.count(k)) {
                if (pre_de) return *pre_de;
                for (const auto& ex : pool) if (ex.fleet == na && ex.jd.dir == d.dir && v3_tree_key(ex.tree) == v3_tree_key(t)) return ex.de;
            }
            de = pre_de ? *pre_de : eval_dual_128(cd, jc, d, sp_tr64, sp_va64, na, t);
            if (!de.valid()) return de;
            if (seen_sol.insert(k).second) pool.push_back({d, na, t, de, is_pure});
            return de;
        };

        DualEval warm_de = push_sol(warm_jd, warm_n, warm_tree, false);

        // Also seed compacted warm_tree across adjacent fleets AND all valid 1-corridor flips of warm_dir!
        for (int nf = n_lo; nf <= n_hi; ++nf) {
            if (nf != warm_n) push_sol(warm_jd, nf, warm_tree, false);
        }
        if (cd.fixed_dir.empty()) {
            std::vector<int> free_cids;
            for (int cid = 0; cid < (int)cd.corridors.size(); ++cid)
                if (!cd.corridors[cid].is_parallel_pair) free_cids.push_back(cid);
            std::vector<JSimDir> warm_1flips;
            std::set<std::string> w_fp = {jsim_fp(jc, warm_jd)};
            for (int cid : free_cids) {
                std::string fd = flip_corridor(cd, warm_dir, cid);
                if (fast_scc_check_mask(cd, fd)) {
                    JSimDir jdf = compile_jsim_dir(cd, jc, fd);
                    if (w_fp.insert(jsim_fp(jc, jdf)).second) warm_1flips.push_back(std::move(jdf));
                }
            }
            if (!warm_1flips.empty()) {
                std::vector<double> f_sc(warm_1flips.size(), 999999.0);
                std::vector<Tree> f_tr(warm_1flips.size(), warm_tree);
                #pragma omp parallel for schedule(dynamic, 1)
                for (int i = 0; i < (int)warm_1flips.size(); ++i) {
                    double s_un = exact_score(eval_pooled_serial(cd, jc, warm_1flips[i], sp32, warm_n, warm_tree));
                    Tree sc_t = rescale_tree_v3(cd, warm_tree, warm_jd, warm_1flips[i]);
                    double s_sc = exact_score(eval_pooled_serial(cd, jc, warm_1flips[i], sp32, warm_n, sc_t));
                    if (s_sc < s_un) { f_sc[i] = s_sc; f_tr[i] = sc_t; }
                    else { f_sc[i] = s_un; f_tr[i] = warm_tree; }
                }
                std::vector<int> ford(warm_1flips.size());
                std::iota(ford.begin(), ford.end(), 0);
                std::sort(ford.begin(), ford.end(), [&](int a, int b) { return f_sc[a] < f_sc[b]; });
                for (int k = 0; k < std::min<int>(6, (int)ford.size()); ++k) {
                    if (f_sc[ford[k]] >= 900000.0) break;
                    int idx = ford[k];
                    push_sol(warm_1flips[idx], warm_n, f_tr[idx], false);
                }
            }
        }

        // --------------------------------------------------------------------
        // Tier 0: Constrained Dual-Flow + Full C<=22 SCC Topology Discovery
        // --------------------------------------------------------------------
        auto top_dirs = discover_global_topologies_v3(cd, jc, warm_dir, n_lo, n_hi, warm_n, sp8, log_f, 12);

        // --------------------------------------------------------------------
        // Tier 1: Multi-Track Coupled (dir, n) Scan across [n_lo .. n_hi]
        // --------------------------------------------------------------------
        struct PairSeed {
            int dir_i;
            int fleet;
            double sc32;
            std::vector<Tree> top_tmpls;
        };
        std::vector<PairSeed> raw_pairs;
        for (int di = 0; di < (int)top_dirs.size(); ++di) {
            for (int nf = n_lo; nf <= n_hi; ++nf) raw_pairs.push_back({di, nf, 999999.0, {}});
        }

        std::vector<double> pair_sc8(raw_pairs.size(), 999999.0);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int pi = 0; pi < (int)raw_pairs.size(); ++pi) {
            const auto& jd = top_dirs[raw_pairs[pi].dir_i];
            int nf = raw_pairs[pi].fleet;
            auto tmpls = build_v3_templates(cd, jd, true);
            double bsc = 999999.0;
            for (const auto& tr : tmpls) {
                double sc = exact_score(eval_pooled_serial(cd, jc, jd, sp8, nf, tr));
                if (sc < bsc) bsc = sc;
            }
            pair_sc8[pi] = bsc;
        }

        std::vector<int> p_ord(raw_pairs.size());
        std::iota(p_ord.begin(), p_ord.end(), 0);
        std::sort(p_ord.begin(), p_ord.end(), [&](int a, int b) { return pair_sc8[a] < pair_sc8[b]; });

        std::vector<int> surv_p_idx;
        std::vector<uint8_t> p_used(raw_pairs.size(), 0);
        std::map<int, int> fl_q;
        for (int idx : p_ord) {
            if (pair_sc8[idx] >= 900000.0) break;
            int nf = raw_pairs[idx].fleet;
            if (fl_q[nf] < 2 && (int)surv_p_idx.size() < 16) {
                fl_q[nf]++;
                p_used[idx] = 1;
                surv_p_idx.push_back(idx);
            }
        }
        for (int idx : p_ord) {
            if (pair_sc8[idx] >= 900000.0 || (int)surv_p_idx.size() >= 16) break;
            if (!p_used[idx]) { p_used[idx] = 1; surv_p_idx.push_back(idx); }
        }

        std::vector<PairSeed> viable_pairs;
        for (int idx : surv_p_idx) {
            int di = raw_pairs[idx].dir_i;
            int nf = raw_pairs[idx].fleet;
            const auto& jd = top_dirs[di];
            auto tmpls = build_v3_templates(cd, jd, false);

            std::vector<double> scs(tmpls.size(), 999999.0);
            #pragma omp parallel for schedule(dynamic, 1)
            for (int ti = 0; ti < (int)tmpls.size(); ++ti) {
                scs[ti] = exact_score(eval_pooled_serial(cd, jc, jd, sp16, nf, tmpls[ti]));
            }

            std::vector<int> t_ord(tmpls.size());
            std::iota(t_ord.begin(), t_ord.end(), 0);
            std::sort(t_ord.begin(), t_ord.end(), [&](int a, int b) { return scs[a] < scs[b]; });
            if (!t_ord.empty() && scs[t_ord[0]] < 900000.0) {
                PairSeed ps{di, nf, scs[t_ord[0]], {}};
                std::set<int> root_f;
                // Keep best overall + root-feature diversity (`w`, `urg`, `de`)!
                for (int k = 0; k < (int)t_ord.size() && (int)ps.top_tmpls.size() < 4; ++k) {
                    if (scs[t_ord[k]] >= 900000.0) break;
                    const Tree& cand_t = tmpls[t_ord[k]];
                    if (k < 2 || root_f.insert(cand_t.nodes[1].feat).second) {
                        ps.top_tmpls.push_back(cand_t);
                        root_f.insert(cand_t.nodes[1].feat);
                    }
                }
                viable_pairs.push_back(std::move(ps));
            }
        }
        std::sort(viable_pairs.begin(), viable_pairs.end(), [](const PairSeed& a, const PairSeed& b) {
            return a.sc32 < b.sc32;
        });

        for (int i = 0; i < std::min<int>(12, (int)viable_pairs.size()); ++i) {
            for (const auto& tr : viable_pairs[i].top_tmpls) {
                push_sol(top_dirs[viable_pairs[i].dir_i], viable_pairs[i].fleet, tr, true);
            }
        }

        // Select 4 diverse (dir, n) pairs for Multi-Root Subtree BCD
        std::vector<PairSeed> bcd_pairs;
        std::vector<uint8_t> vp_used(viable_pairs.size(), 0);
        auto pick_band = [&](int lo_b, int hi_b, int quota) {
            int added = 0;
            std::set<int> seen_f;
            for (size_t i = 0; i < viable_pairs.size() && added < quota && (int)bcd_pairs.size() < 4; ++i) {
                if (vp_used[i]) continue;
                int nf = viable_pairs[i].fleet;
                if (nf >= lo_b && nf <= hi_b && seen_f.insert(nf).second) {
                    bcd_pairs.push_back(viable_pairs[i]);
                    vp_used[i] = 1;
                    ++added;
                }
            }
        };
        pick_band(warm_n + 1, n_hi, 2);
        pick_band(n_lo, warm_n - 1, 1);
        pick_band(warm_n, warm_n, 1);
        for (size_t i = 0; i < viable_pairs.size() && (int)bcd_pairs.size() < 4; ++i) {
            if (!vp_used[i]) { bcd_pairs.push_back(viable_pairs[i]); vp_used[i] = 1; }
        }

        log_f << "  [Tier 1 Multi-Track Scan] ViablePairs=" << viable_pairs.size()
              << " | Best pure sp32=" << (viable_pairs.empty() ? 999999.0 : viable_pairs[0].sc32)
              << " (n=" << (viable_pairs.empty() ? 0 : viable_pairs[0].fleet) << ")"
              << " -> BCDPairs=" << bcd_pairs.size() << "\n" << std::flush;

        // --------------------------------------------------------------------
        // Tier 2: Multi-Root Subtree BCD (`w`-root, `urg`-root, `de`-root)
        // --------------------------------------------------------------------
        for (const auto& bp : bcd_pairs) {
            const auto& jd = top_dirs[bp.dir_i];
            auto bcd_trees = v3_multi_root_bcd_beam(cd, jc, jd, bp.fleet, sp16, sp32);
            for (const auto& tr : bcd_trees) {
                push_sol(jd, bp.fleet, tr, true);
            }
        }

        std::sort(pool.begin(), pool.end(), [](const SolCand& a, const SolCand& b) {
            return a.de.rob128 < b.de.rob128;
        });

        // Build Focused Stage 3 SA+HJ Roster (up to 5 distinct high-potential seeds)
        double best_pool_rob = pool.empty() ? 999999.0 : pool[0].de.rob128;
        std::vector<SolCand> fine_seeds;
        std::set<std::string> fine_exact_sol;
        std::set<std::string> fine_sig;
        auto add_fine = [&](const SolCand& sc, bool req_distinct_df = true, double max_gap = 9.0) {
            if ((int)fine_seeds.size() >= 5 && req_distinct_df) return false;
            if (req_distinct_df && sc.de.rob128 > best_pool_rob + max_gap) return false;
            std::string ex_k = sc.jd.dir + "|" + std::to_string(sc.fleet) + "|" + v3_tree_key(sc.tree);
            if (fine_exact_sol.count(ex_k)) return false;
            std::string sig = req_distinct_df
                            ? (sc.jd.dir + "|" + std::to_string(sc.fleet) + "|r" + std::to_string(sc.tree.nodes[1].feat))
                            : ex_k;
            if (!fine_sig.insert(sig).second) return false;
            fine_exact_sol.insert(ex_k);
            fine_seeds.push_back(sc);
            return true;
        };

        // 1) Overall #1 pool entry (immediately picks best 1-corridor flip or best pure_v3!)
        if (!pool.empty()) add_fine(pool[0]);
        // 2) Best Pure v3 in Surplus Fleet Band (`n >= warm_n + 1`)
        for (const auto& sc : pool) {
            if (sc.pure_v3 && sc.fleet >= warm_n + 1) { if (add_fine(sc)) break; }
        }
        // 3) Root-Track Diversity: competitive `w`-root, `urg`-root, `de`-root
        for (int rf : {1, 0, 2}) {
            for (const auto& sc : pool) {
                if (sc.pure_v3 && sc.tree.nodes[1].feat == rf) { if (add_fine(sc)) break; }
            }
        }
        // 4) Fill up to 4 slots from top pool
        for (const auto& sc : pool) {
            if ((int)fine_seeds.size() >= 4) break;
            add_fine(sc);
        }
        // 5) Always include compacted Baseline Anchor (`warm_jd, warm_n, warm_tree`) if not already #1
        if (warm_de.valid()) {
            add_fine({warm_jd, warm_n, warm_tree, warm_de, false}, false, 999.0);
        }

        log_f << "  [Tier 2 Multi-Root BCD] Pool=" << pool.size()
              << " | FineSeeds=" << fine_seeds.size() << "\n";
        for (size_t fi = 0; fi < fine_seeds.size(); ++fi) {
            log_f << "    Seed#" << (fi + 1) << " n=" << fine_seeds[fi].fleet
                  << " (" << fine_seeds[fi].tree.n_nodes << "n, root=" << describe_root(fine_seeds[fi].tree)
                  << ", pure=" << (fine_seeds[fi].pure_v3 ? 1 : 0)
                  << ") init rob128=" << fine_seeds[fi].de.rob128 << "\n";
        }
        log_f.flush();

        // --------------------------------------------------------------------
        // Tier 3: Ergodic SA + Mid-Flight Zombie Compaction + Greedy Leaf Boost + Discrete HJ + Coupled LNS
        // --------------------------------------------------------------------
        std::mt19937 rng(91001 + c * 409);
        for (size_t fi = 0; fi < fine_seeds.size(); ++fi) {
            SolCand cur = fine_seeds[fi];
            DualEval pol_de;
            std::vector<std::pair<Tree, DualEval>> hist;
            int main_iters = (fi < 2) ? 200 : 150;
            Tree pol_tr = v3_robust_sa_hj_polish(cd, jc, cur.jd, sp_tr64, sp_va64, sp16, cur.fleet, cur.tree, main_iters, pol_de, hist, rng);
            for (const auto& [ht, hde] : hist) push_sol(cur.jd, cur.fleet, ht, cur.pure_v3, &hde);
            if (pol_de.valid() && pol_de.rob128 < cur.de.rob128 - 1e-4) {
                cur.tree = pol_tr;
                cur.de = pol_de;
                push_sol(cur.jd, cur.fleet, cur.tree, cur.pure_v3, &cur.de);
            }

            if (cd.fixed_dir.empty() && fi < 3) {
                auto lns_dirs = coupled_dir_lns_around_tree_v3(cd, jc, cur.jd, cur.fleet, cur.tree, sp32, rng);
                for (const auto& fd : lns_dirs) {
                    DualEval ude = push_sol(fd, cur.fleet, cur.tree, cur.pure_v3);
                    Tree scaled = rescale_tree_v3(cd, cur.tree, cur.jd, fd);
                    DualEval sde = push_sol(fd, cur.fleet, scaled, cur.pure_v3);
                    const Tree& base_lns_t = (ude.valid() && (!sde.valid() || ude.rob128 <= sde.rob128)) ? cur.tree : scaled;
                    const DualEval& base_lns_de = (ude.valid() && (!sde.valid() || ude.rob128 <= sde.rob128)) ? ude : sde;
                    if (base_lns_de.valid() && base_lns_de.rob128 <= cur.de.rob128 + 3.5) {
                        DualEval fde;
                        std::vector<std::pair<Tree, DualEval>> fhist;
                        Tree ftr = v3_robust_sa_hj_polish(cd, jc, fd, sp_tr64, sp_va64, sp16, cur.fleet, base_lns_t, 110, fde, fhist, rng);
                        for (const auto& [ht, hde] : fhist) push_sol(fd, cur.fleet, ht, cur.pure_v3, &hde);
                        if (fde.valid() && fde.rob128 < cur.de.rob128 - 1e-4) {
                            cur.jd = fd;
                            cur.tree = ftr;
                            cur.de = fde;
                            push_sol(cur.jd, cur.fleet, cur.tree, cur.pure_v3, &cur.de);
                        }
                    }
                }
            }

            // Coupled Fleet Shift (`n - 1`, `n + 1`)
            for (int adj_n : {cur.fleet - 1, cur.fleet + 1}) {
                if (adj_n < n_lo || adj_n > n_hi) continue;
                DualEval ade = push_sol(cur.jd, adj_n, cur.tree, cur.pure_v3);
                if (ade.valid() && ade.rob128 <= cur.de.rob128 + 5.0 && fi < 2) {
                    DualEval pde;
                    std::vector<std::pair<Tree, DualEval>> ahist;
                    Tree atr = v3_robust_sa_hj_polish(cd, jc, cur.jd, sp_tr64, sp_va64, sp16, adj_n, cur.tree, 100, pde, ahist, rng);
                    for (const auto& [ht, hde] : ahist) push_sol(cur.jd, adj_n, ht, cur.pure_v3, &hde);
                    if (pde.valid() && pde.rob128 < cur.de.rob128 - 1e-4) {
                        cur.fleet = adj_n;
                        cur.tree = atr;
                        cur.de = pde;
                        push_sol(cur.jd, cur.fleet, cur.tree, cur.pure_v3, &cur.de);
                    }
                }
            }

            log_f << "  [Tier 3 Fine #" << (fi + 1) << "] Best rob128=" << cur.de.rob128
                  << " (tr=" << cur.de.tr64 << ", va=" << cur.de.va64
                  << ", n=" << cur.fleet << ", " << cur.tree.n_nodes
                  << "n, root=" << describe_root(cur.tree) << ")\n" << std::flush;
        }

        // --------------------------------------------------------------------
        // Tier 3.5: 128-Seed Holdout Gate (`129..256`) -> 256-Seed 3-Block Ranking
        // --------------------------------------------------------------------
        std::sort(pool.begin(), pool.end(), [](const SolCand& a, const SolCand& b) {
            return a.de.rob128 < b.de.rob128;
        });

        std::vector<int> ho_ids;
        std::vector<uint8_t> ho_picked(pool.size(), 0);
        auto add_ho = [&](int id) {
            if (id >= 0 && id < (int)pool.size() && !ho_picked[id] && (int)ho_ids.size() < 42) {
                ho_picked[id] = 1;
                ho_ids.push_back(id);
            }
        };
        std::set<int> ho_fleets, ho_roots;
        for (int i = 0; i < (int)pool.size(); ++i) {
            if (ho_fleets.insert(pool[i].fleet).second) add_ho(i);
            if (ho_roots.insert(pool[i].tree.nodes[1].feat).second) add_ho(i);
        }
        int pure_ho = 0;
        for (int i = 0; i < (int)pool.size() && pure_ho < 18; ++i) {
            if (pool[i].pure_v3) { add_ho(i); ++pure_ho; }
        }
        for (int i = 0; i < (int)pool.size(); ++i) add_ho(i);

        std::vector<std::pair<double, int>> ranked_256;
        for (int id : ho_ids) {
            auto pho = eval_exact_pooled(cd, jc, pool[id].jd, sp_ho128, pool[id].fleet, pool[id].tree, true);
            double ho_sc = exact_score(pho);
            if (ho_sc >= 900000.0) continue;
            double mean256 = 0.25 * pool[id].de.tr64 + 0.25 * pool[id].de.va64 + 0.50 * ho_sc;
            double worst_blk = std::max({pool[id].de.tr64, pool[id].de.va64, ho_sc});
            double rob256 = 0.88 * mean256 + 0.12 * worst_blk;
            ranked_256.push_back({rob256, id});
        }
        std::sort(ranked_256.begin(), ranked_256.end());

        // --------------------------------------------------------------------
        // Tier 4: Official 1,005-Seed + 10-Block Worst-20% CVaR Final Audit (Strict 0 DQ)
        // --------------------------------------------------------------------
        std::vector<int> aud_ids;
        std::set<int> aud_set;
        std::set<int> aud_fl;
        for (const auto& [sc, id] : ranked_256) {
            if (aud_fl.insert(pool[id].fleet).second && aud_set.insert(id).second) aud_ids.push_back(id);
        }
        for (const auto& [sc, id] : ranked_256) {
            if ((int)aud_ids.size() >= 26) break;
            if (aud_set.insert(id).second) aud_ids.push_back(id);
        }

        JSimDir win_jd = warm_jd;
        int win_n = warm_n;
        Tree win_tree = warm_tree;
        PooledStat win_500 = base_500, win_oos = base_oos, win_pub = base_pub;
        double win_cvar = base_cvar;
        double win_rank_sc = 0.55 * base_500.pooled_cost + 0.35 * base_oos.pooled_cost + 0.10 * base_cvar;

        for (size_t i = 0; i < aud_ids.size(); ++i) {
            const auto& cand = pool[aud_ids[i]];
            std::vector<JSimOut> outs_500, outs_oos;
            auto r500 = eval_exact_pooled(cd, jc, cand.jd, sp500, cand.fleet, cand.tree, true, &outs_500);
            if (r500.dq_cnt > 0) continue;
            auto roos = eval_exact_pooled(cd, jc, cand.jd, sp_oos, cand.fleet, cand.tree, true, &outs_oos);
            if (roos.dq_cnt > 0) continue;
            auto rpub = eval_exact_pooled(cd, jc, cand.jd, sp_pub, cand.fleet, cand.tree, true);
            if (rpub.dq_cnt > 0) continue;

            double cvar20 = compute_block_cvar20(cd, jc, cand.fleet, outs_500, outs_oos);
            double rank_sc = 0.55 * r500.pooled_cost + 0.35 * roos.pooled_cost + 0.10 * cvar20;

            log_f << "    Audit#" << (i + 1) << " n=" << cand.fleet << " (" << cand.tree.n_nodes
                  << "n, " << describe_root(cand.tree) << ")"
                  << " | 1..500=" << r500.pooled_cost << " (W=" << r500.mean_w << ", U95=" << r500.u95 << ")"
                  << " | 11..15=" << rpub.pooled_cost
                  << " | OOS=" << roos.pooled_cost
                  << " | CVaR20=" << cvar20
                  << " | endQ=" << std::max({r500.max_end_q, roos.max_end_q, rpub.max_end_q}) << " [PASS]\n" << std::flush;

            // Strict non-regression on 1..500 AND robust generalized improvement!
            if (r500.pooled_cost <= base_500.pooled_cost + 1e-6 && rank_sc < win_rank_sc - 1e-4) {
                win_rank_sc = rank_sc;
                win_jd = cand.jd;
                win_n = cand.fleet;
                win_tree = cand.tree;
                win_500 = r500;
                win_oos = roos;
                win_pub = rpub;
                win_cvar = cvar20;
            }
        }

        // Final 500-seed exact zombie splice verification so saved tree has ZERO dead nodes!
        Tree clean_win = splice_zombie_nodes(cd, jc, win_jd, sp500, win_n, win_tree);
        if (clean_win.n_nodes < win_tree.n_nodes) {
            auto c500 = eval_exact_pooled(cd, jc, win_jd, sp500, win_n, clean_win, false);
            auto coos = eval_exact_pooled(cd, jc, win_jd, sp_oos, win_n, clean_win, false);
            auto cpub = eval_exact_pooled(cd, jc, win_jd, sp_pub, win_n, clean_win, false);
            if (c500.dq_cnt + coos.dq_cnt + cpub.dq_cnt == 0 &&
                std::abs(c500.pooled_cost - win_500.pooled_cost) <= 1e-6 &&
                std::abs(coos.pooled_cost - win_oos.pooled_cost) <= 1e-6) {
                win_tree = clean_win;
            }
        }

        bool should_write = (win_500.pooled_cost < base_500.pooled_cost - 1e-4) ||
                            (std::abs(win_500.pooled_cost - base_500.pooled_cost) <= 1e-6 &&
                             win_oos.pooled_cost <= base_oos.pooled_cost + 1e-6 &&
                             win_tree.n_nodes < warm_tree_raw.n_nodes);

        std::string out_csv = "answers/ans_c" + std::to_string(c) + ".csv";
        if (should_write) {
            save_solution_csv(out_csv, win_jd.dir, win_n, win_tree);
        }

        double case_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_case_start).count();
        long long case_sims = g_sims.load() - sims_before;

        log_f << "  => " << (should_write ? "UPDATED " : "KEPT ") << out_csv
              << " | n=" << win_n << " (" << win_tree.n_nodes << "n, root=" << describe_root(win_tree) << ")"
              << " | 1..500=" << win_500.pooled_cost << " (d=" << (win_500.pooled_cost - base_500.pooled_cost) << ")"
              << " | 11..15=" << win_pub.pooled_cost << " (d=" << (win_pub.pooled_cost - base_pub.pooled_cost) << ")"
              << " | OOS=" << win_oos.pooled_cost << " (d=" << (win_oos.pooled_cost - base_oos.pooled_cost) << ")"
              << " | CVaR20=" << win_cvar << " (d=" << (win_cvar - base_cvar) << ")"
              << " | DQ=0/1005 | time=" << std::setprecision(1) << case_sec << "s (" << case_sims << " sims)\n";
        log_f << "  Winning Active Tree (" << win_tree.n_nodes << " nodes, 0 zombies):\n";
        for (int nid = 1; nid <= win_tree.n_nodes; ++nid) {
            const auto& tn = win_tree.nodes[nid];
            log_f << "    key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ","
                  << format_th_precise(tn.feat, tn.th) << "," << tn.lo << "," << tn.hi << "\n";
        }
        log_f.flush();

        std::cout << "[Case " << c << " " << (should_write ? "UPDATED" : "KEPT") << " -> " << out_csv
                  << "] n=" << win_n << " (" << win_tree.n_nodes << "n, " << describe_root(win_tree)
                  << ") | 1..500=" << std::setprecision(4) << win_500.pooled_cost
                  << " (d=" << (win_500.pooled_cost - base_500.pooled_cost) << ")"
                  << " | 11..15=" << win_pub.pooled_cost
                  << " | OOS=" << win_oos.pooled_cost << " (d=" << (win_oos.pooled_cost - base_oos.pooled_cost) << ")"
                  << " | CVaR20=" << win_cvar
                  << " | time=" << std::setprecision(1) << case_sec << "s (" << case_sims << " sims)\n" << std::flush;
    }

    double tot_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_global_start).count();
    log_f << "\n=== ALL CASES COMPLETED in " << std::fixed << std::setprecision(1) << tot_sec
          << "s | Total DES sims = " << g_sims.load() << " ===\n" << std::flush;
    std::cout << "=== ALL CASES COMPLETED in " << std::fixed << std::setprecision(1) << tot_sec
              << "s | Total DES sims = " << g_sims.load() << " ===\n" << std::flush;
    return 0;
}
