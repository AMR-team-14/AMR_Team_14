#include "../src/common/io.hpp"
#include <iomanip>
#include <map>

// Deep Physics Lab 1 (200 seeds on current v3 ans_c1..c5.csv):
// Exp 1A: Separate Fixed 0.36t Junction Occupancy vs Dynamic Contention Queueing (JuncWait == 0 counterfactual)
// Exp 1B: U95 Staircase Decomposition (Exact 90th..99th percentile origin x empty-pickup pairs)
// Exp 1C: Proactive Idle Parking Vacuum Bound (What if idle AGVs reposition to top urgent-deficit station?)
// Exp 1D: Worst-10 vs Best-10 Seed Anatomy (Arrival burst vs Spatial skew vs Cascade)

struct JSimRoute { int nl = 0; std::vector<int> jn; std::vector<double> la; double total = 0.0; };
struct JSimCase { int nd = 0, nJ = 0; std::vector<int> JIDX, JNODE; double RATE = 0, FAC = 1.2, MEAN = 1.0, LAM = 0; };
struct JSimDir { std::string dir; std::vector<std::vector<double>> DIST; std::vector<std::vector<JSimRoute>> ROUTE; };
struct JSimArrival { int qo, qd; double qt; int qi; };
struct JSimStream { int seed = 0; std::vector<JSimArrival> arr; };

static JSimCase build_jsim_case(const CaseData& cd) {
    JSimCase jc; jc.nd = cd.n_stations;
    std::vector<int> deg(cd.n_nodes, 0);
    for (const auto& e : cd.edges) { deg[e.u]++; deg[e.v]++; }
    jc.JIDX.assign(cd.n_nodes, -1); jc.nJ = 0;
    for (int v = 0; v < cd.n_nodes; ++v) if (deg[v] >= 3) { jc.JIDX[v] = jc.nJ++; jc.JNODE.push_back(v); }
    for (const auto& f : cd.flows) jc.RATE += f.rate;
    jc.FAC = 60.0 / cd.at; jc.MEAN = 60.0 / (cd.alpha * jc.RATE); jc.LAM = cd.alpha * jc.RATE;
    return jc;
}

static JSimDir compile_jsim_dir(const CaseData& cd, const JSimCase& jc, const std::string& bits) {
    JSimDir jd; jd.dir = bits;
    const int V = cd.n_nodes; const double INF = 1e18;
    std::vector<std::vector<double>> D(V, std::vector<double>(V, INF));
    std::vector<std::vector<int>> NX(V, std::vector<int>(V, -1));
    for (int i = 0; i < V; ++i) { D[i][i] = 0; NX[i][i] = i; }
    for (int k = 0; k < cd.n_edges; ++k) {
        int u = cd.edges[k].u, v = cd.edges[k].v;
        if (bits[k] == '1') std::swap(u, v);
        if (cd.edges[k].dist < D[u][v]) { D[u][v] = cd.edges[k].dist; NX[u][v] = v; }
    }
    for (int k = 0; k < V; ++k)
        for (int i = 0; i < V; ++i)
            for (int j = 0; j < V; ++j)
                if (D[i][k] + D[k][j] < D[i][j]) { D[i][j] = D[i][k] + D[k][j]; NX[i][j] = NX[i][k]; }
    jd.DIST.assign(jc.nd, std::vector<double>(jc.nd, 0.0));
    jd.ROUTE.assign(jc.nd, std::vector<JSimRoute>(jc.nd));
    for (int a = 0; a < jc.nd; ++a) {
        for (int b = 0; b < jc.nd; ++b) {
            int pa = cd.pd[a], pb = cd.pd[b];
            jd.DIST[a][b] = D[pa][pb];
            JSimRoute rt; rt.total = D[pa][pb];
            int c = pa;
            while (c != pb && c >= 0) {
                c = NX[c][pb];
                if (c != pb && c >= 0 && jc.JIDX[c] >= 0) { rt.jn.push_back(jc.JIDX[c]); rt.la.push_back(D[pa][c]); }
            }
            rt.nl = (int)rt.jn.size();
            jd.ROUTE[a][b] = std::move(rt);
        }
    }
    return jd;
}

static JSimStream generate_jsim_stream(const CaseData& cd, const JSimCase& jc, int seed) {
    JSimStream sp; sp.seed = seed; uint32_t rs = (uint32_t)seed;
    auto R = [&]() -> double {
        rs = rs + 0x6D2B79F5u;
        uint32_t t = (rs ^ (rs >> 15)) * (1u | rs);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    };
    auto EXP = [&]() -> double {
        int k = 0;
        for (;;) { double u = R(), prev = u; int m = 1; for (;;) { double v = R(); if (v < prev) { prev = v; ++m; } else break; } if (m % 2 == 1) return (double)k + u; ++k; }
    };
    double next = EXP() * jc.MEAN; int serial = 0;
    while (next <= cd.t_end) {
        double u = R() * jc.RATE; int k = 0;
        while (k < cd.n_flows - 1 && u >= cd.flows[k].rate) { u -= cd.flows[k].rate; ++k; }
        sp.arr.push_back({cd.flows[k].o_st, cd.flows[k].d_st, next, serial++});
        next += EXP() * jc.MEAN;
    }
    return sp;
}

struct UrgStep { double w; int from_st; int o_st; };

struct SimOut1 {
    bool dq = false;
    double sum_w = 0.0, u95_seed = 0.0, cost_seed = 0.0;
    int cnt_w = 0, n_arr_warm = 0, max_q = 0, max_burst_5m = 0;
    std::vector<double> wu;
    std::vector<UrgStep> steps;
};

// mode: 0=exact, 1=no_junc_queue (fixed 0.36t kept, parallel crossing!), 2=zero_036t, 3=reloc_vacuum (idle moves to target_reloc_st)
static SimOut1 run_sim_mode(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                            const JSimStream& sp, int n, const Tree& tree, int mode, int reloc_st = -1)
{
    SimOut1 out;
    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest;
    const double T = cd.t_end, warm = cd.warm, FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC;
    const double pf = (mode == 2) ? 0.0 : (cd.p_occ * FAC);
    const double INF = std::numeric_limits<double>::infinity();

    int vnode[64], vst[64], vph[64], vto[64], vk[64], vro[64], vrd[64], vfrom[64];
    double vspd[64], vpos[64], vd1[64], tact[64], vrtm[64];
    const JSimRoute* vrt[64];
    for (int i = 0; i < n; ++i) {
        vnode[i]=i%nd; vst[i]=IDLE; vph[i]=0; vto[i]=0; vk[i]=0; vro[i]=0; vrd[i]=0; vfrom[i]=vnode[i];
        vspd[i]=0; vpos[i]=0; vd1[i]=0; tact[i]=INF; vrtm[i]=0; vrt[i]=nullptr;
    }
    int jbusy[128], jh[128], jt[128], jq[128*64];
    for (int g=0; g<nJ; ++g) { jbusy[g]=-1; jh[g]=0; jt[g]=0; }
    int qo_[512], qd_[512], qi_[512], ql=0, nIdle=n, nWq=0, maxQ=0;
    double qt_[512], t=0.0, key[16], bk[16];
    int idle[64], qcnt[32];
    const int n_arr = (int)sp.arr.size(); int aidx = 0;

    // 5-min arrival burst count
    int l_b = 0;
    for (int r = 0; r < n_arr; ++r) {
        if (sp.arr[r].qt >= warm) out.n_arr_warm++;
        while (sp.arr[r].qt - sp.arr[l_b].qt > 5.0) l_b++;
        if (sp.arr[r].qt >= warm) out.max_burst_5m = std::max(out.max_burst_5m, r - l_b + 1);
    }

    auto startLeg = [&](int i) {
        const JSimRoute* rt = vrt[i];
        double d1 = (vk[i] < rt->nl) ? rt->la[vk[i]] : rt->total;
        vst[i] = DRV; vd1[i] = d1; tact[i] = t + (d1 - vpos[i]) / vspd[i] * FAC;
    };
    auto travel = [&](int i, int ph, int to, double spd) {
        vrt[i] = &jd.ROUTE[vnode[i]][to]; vph[i]=ph; vto[i]=to; vspd[i]=spd; vk[i]=0; vpos[i]=0; startLeg(i);
    };
    auto handle = [&](int i) {
        int st = vst[i];
        if (st == DRV) {
            vpos[i] = vd1[i]; const JSimRoute* rt = vrt[i];
            if (vk[i] < rt->nl) {
                if (mode == 1) { // No contention queueing, pay only fixed pf!
                    vst[i] = CRS; tact[i] = t + pf; return;
                }
                vst[i]=WQ; tact[i]=INF; int j=rt->jn[vk[i]]; jq[j*64+(jt[j]++&63)]=i; nWq++; return;
            }
            vnode[i] = vto[i];
            if (vph[i] == 0) {
                double w = t - vrtm[i];
                if (vrtm[i] >= warm) {
                    out.sum_w += w; out.cnt_w++;
                    if (vrd[i] == UD) {
                        out.wu.push_back(w);
                        if (mode == 0) out.steps.push_back({w, vfrom[i], vro[i]});
                    }
                }
                vst[i] = LD; tact[i] = t + tlf;
            } else if (vph[i] == 1) {
                vst[i] = UL; tact[i] = t + tuf;
            } else {
                // Finished proactive empty relocation (ph==2)
                vst[i] = IDLE; tact[i] = INF; nIdle++;
            }
        } else if (st == CRS) {
            if (mode != 1) jbusy[vrt[i]->jn[vk[i]]] = -1;
            vk[i]++; startLeg(i);
        } else if (st == LD) {
            travel(i, 1, vrd[i], cd.vc);
        } else if (st == UL) {
            if (mode == 3 && reloc_st >= 0 && vnode[i] != reloc_st && ql == 0 && nIdle >= 3) {
                // Proactive relocation when surplus idle >= 3 and queue is empty!
                travel(i, 2, reloc_st, cd.ve);
            } else {
                vst[i] = IDLE; tact[i] = INF; nIdle++;
            }
        }
    };
    auto dispatch = [&]() {
        while (nIdle > 0 && ql > 0) {
            int ni=0; for (int m=0; m<n; ++m) if (vst[m]==IDLE) idle[ni++]=m;
            for (int m=0; m<nd; ++m) qcnt[m]=0;
            for (int m=0; m<ql; ++m) qcnt[qo_[m]]++;
            int bv=-1, bb=-1, bl=0;
            for (int a=0; a<ni; ++a) {
                int vi=idle[a], vn=vnode[vi]; const auto& dist_vn=jd.DIST[vn];
                for (int b=0; b<ql; ++b) {
                    int ro=qo_[b]; double de=dist_vn[ro]; int kl=0, id=1;
                    for (int g=0; g<8 && id!=0; ++g) {
                        const TreeNode& tn=tree.nodes[id]; int ff=tn.feat, side;
                        if (ff==0) side=(qd_[b]==UD)?1:0;
                        else if (ff==1) side=((t-qt_[b])<=tn.th)?0:1;
                        else { double val=(ff==2)?de:(ff==3)?jd.DIST[ro][qd_[b]]:(double)qcnt[ro]; side=(val<=tn.th)?0:1; }
                        key[kl++]=tn.is_min?(double)side:(double)(1-side);
                        id=side?tn.hi:tn.lo;
                    }
                    key[kl++]=de; key[kl++]=qt_[b]; key[kl++]=(double)qi_[b]; key[kl++]=(double)vi;
                    bool less=(bv<0);
                    if (!less) { int lim=(kl<bl)?kl:bl; for (int i=0;i<lim;++i) { if (key[i]<bk[i]){less=true;break;} if (key[i]>bk[i])break; } }
                    if (less) { for (int c=0;c<kl;++c) bk[c]=key[c]; bl=kl; bv=vi; bb=b; }
                }
            }
            vfrom[bv]=vnode[bv]; vro[bv]=qo_[bb]; vrd[bv]=qd_[bb]; vrtm[bv]=qt_[bb];
            for (int m=bb; m<ql-1; ++m) { qo_[m]=qo_[m+1]; qd_[m]=qd_[m+1]; qt_[m]=qt_[m+1]; qi_[m]=qi_[m+1]; }
            ql--; nIdle--; travel(bv, 0, vro[bv], cd.ve);
        }
    };
    for (;;) {
        double next_arr=(aidx<n_arr)?sp.arr[aidx].qt:INF, tn=next_arr;
        for (int m=0; m<n; ++m) if (tact[m]<tn) tn=tact[m];
        if (tn>T) break;
        t=tn;
        while (aidx<n_arr && sp.arr[aidx].qt<=t) {
            if (ql>=512) { out.dq=true; return out; }
            qo_[ql]=sp.arr[aidx].qo; qd_[ql]=sp.arr[aidx].qd; qt_[ql]=sp.arr[aidx].qt; qi_[ql]=sp.arr[aidx].qi;
            ql++; aidx++;
        }
        for (;;) {
            for (int i=0; i<n; ++i) while (tact[i]<=t) handle(i);
            if (mode != 1 && nWq>0) for (int g=0; g<nJ; ++g) {
                if (jbusy[g]>=0||jh[g]==jt[g]) continue;
                int i=jq[g*64+(jh[g]++&63)]; vst[i]=CRS; tact[i]=t+pf; jbusy[g]=i; nWq--;
            }
            dispatch();
            bool any=false; for (int i=0; i<n; ++i) if (tact[i]<=t) { any=true; break; }
            if (!any) break;
        }
        maxQ = std::max(maxQ, ql);
        if (ql>cd.qcap) { out.dq=true; return out; }
    }
    out.max_q = maxQ;
    if (ql > cd.end_qmax) out.dq = true;
    if (!out.dq && out.cnt_w > 0 && !out.wu.empty()) {
        std::vector<double> swu = out.wu;
        std::sort(swu.begin(), swu.end());
        out.u95_seed = swu[(size_t)std::floor(0.95 * (swu.size() - 1))];
        out.cost_seed = cd.c_agv * n + cd.c_w * jc.LAM * (out.sum_w / out.cnt_w) + cd.c_u * out.u95_seed;
    }
    return out;
}

struct PooledRes { double cost = 999999, w = 999, u95 = 999; int dq = 0; };
static PooledRes eval_mode_pool(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                                const std::vector<JSimStream>& sps, int n, const Tree& tr, int mode,
                                int reloc_st = -1, std::vector<SimOut1>* raw_out = nullptr)
{
    int S = (int)sps.size();
    std::vector<SimOut1> outs(S);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < S; ++i) outs[i] = run_sim_mode(cd, jc, jd, sps[i], n, tr, mode, reloc_st);
    PooledRes pr;
    double tw = 0; long long tc = 0;
    std::vector<double> wu; wu.reserve(S * 120);
    for (int i = 0; i < S; ++i) {
        if (outs[i].dq) pr.dq++;
        tw += outs[i].sum_w; tc += outs[i].cnt_w;
        wu.insert(wu.end(), outs[i].wu.begin(), outs[i].wu.end());
    }
    if (raw_out) *raw_out = std::move(outs);
    if (pr.dq == 0 && tc > 0 && !wu.empty()) {
        std::sort(wu.begin(), wu.end());
        pr.w = tw / tc;
        pr.u95 = wu[(size_t)std::floor(0.95 * (wu.size() - 1))];
        pr.cost = cd.c_agv * n + cd.c_w * jc.LAM * pr.w + cd.c_u * pr.u95;
    }
    return pr;
}

int main() {
    std::cout << std::fixed << std::setprecision(4);
    const int N_SEEDS = 200;
    std::cout << "========================================================================\n";
    std::cout << "DEEP PHYSICS LAB 1 (200 seeds: Contention vs Fixed 0.36t, U95 Steps, Vacuum)\n";
    std::cout << "========================================================================\n\n";

    int reloc_targets[6] = {0, 0, 0, 0, 0, 0}; // will find top urgent-deficit station dynamically!
    (void)reloc_targets;

    for (int c = 1; c <= 5; ++c) {
        CaseData cd = load_case("data/c" + std::to_string(c) + ".json", "c" + std::to_string(c));
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c" + std::to_string(c) + ".csv");
        JSimDir jd = compile_jsim_dir(cd, jc, ans_dir);

        std::vector<JSimStream> sps(N_SEEDS);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < N_SEEDS; ++i) sps[i] = generate_jsim_stream(cd, jc, i + 1);

        std::vector<SimOut1> exact_outs;
        auto r0 = eval_mode_pool(cd, jc, jd, sps, ans_n, ans_tree, 0, -1, &exact_outs);
        auto r1 = eval_mode_pool(cd, jc, jd, sps, ans_n, ans_tree, 1); // No contention queue (fixed 0.36t kept)
        auto r2 = eval_mode_pool(cd, jc, jd, sps, ans_n, ans_tree, 2); // Zero 0.36t completely

        // Top urgent origin for proactive relocation test
        int top_u_st = 0; double top_u_r = -1;
        for (int s = 0; s < jc.nd; ++s) {
            if (cd.st_urg_orig[s] > top_u_r) { top_u_r = cd.st_urg_orig[s]; top_u_st = s; }
        }
        // Test all high-urgent origins for proactive relocation bound
        PooledRes best_reloc; int best_rst = -1;
        for (int s = 0; s < jc.nd; ++s) {
            if (cd.st_urg_orig[s] <= 0.1) continue;
            auto rr = eval_mode_pool(cd, jc, jd, sps, ans_n, ans_tree, 3, s);
            if (rr.dq == 0 && (best_rst < 0 || rr.cost < best_reloc.cost)) {
                best_reloc = rr; best_rst = s;
            }
        }

        std::cout << ">>> CASE " << c << " (n=" << ans_n << ", 200-seed Exact=" << r0.cost
                  << ", W=" << r0.w << ", U95=" << r0.u95 << ") <<<\n";
        std::cout << "  [1A. 0.36t Contention Queue vs Fixed Occupancy]\n"
                  << "      No-Contention (keep 0.36t fixed): " << r1.cost << " (d=" << (r1.cost - r0.cost)
                  << ", W=" << r1.w << ", U95=" << r1.u95 << ")\n"
                  << "      Zero-0.36t (remove 0.36t total) : " << r2.cost << " (d=" << (r2.cost - r0.cost)
                  << ", W=" << r2.w << ", U95=" << r2.u95 << ")\n";
        double cont_share = (r0.cost - r1.cost) / std::max(1e-6, r0.cost - r2.cost) * 100.0;
        std::cout << "      => Contention Queueing Share of 0.36t Cost = " << std::setprecision(1) << cont_share
                  << "% | Fixed Occupancy Share = " << (100.0 - cont_share) << "%\n" << std::setprecision(4);

        // 1B. U95 Staircase Decomposition
        std::vector<UrgStep> all_steps;
        for (const auto& o : exact_outs) all_steps.insert(all_steps.end(), o.steps.begin(), o.steps.end());
        std::sort(all_steps.begin(), all_steps.end(), [](const UrgStep& a, const UrgStep& b) { return a.w < b.w; });
        std::cout << "  [1B. U95 Staircase Steps (92% -> 95% -> 98%)]\n";
        for (double q : {0.92, 0.95, 0.97, 0.98}) {
            size_t idx = (size_t)std::floor(q * (all_steps.size() - 1));
            std::map<std::pair<int,int>, int> pair_cnt;
            size_t lo = (idx > 120) ? (idx - 120) : 0, hi = std::min(all_steps.size(), idx + 120);
            for (size_t k = lo; k < hi; ++k) pair_cnt[{all_steps[k].from_st, all_steps[k].o_st}]++;
            std::vector<std::pair<int, std::pair<int,int>>> pv;
            for (auto& kv : pair_cnt) pv.push_back({kv.second, kv.first});
            std::sort(pv.rbegin(), pv.rend());
            std::cout << "      P" << (int)(q*100) << "=" << std::setprecision(3) << all_steps[idx].w << "m (top:";
            for (int k = 0; k < std::min(2, (int)pv.size()); ++k) {
                auto [fst, ost] = pv[k].second;
                std::cout << " " << cd.depts[fst] << "->" << cd.depts[ost] << "[" << (int)jd.DIST[fst][ost] << "m,"
                          << std::setprecision(1) << (100.0 * pv[k].first / (hi - lo)) << "%]";
            }
            std::cout << ")\n";
        }

        // 1C. Proactive Parking Vacuum Bound
        std::cout << std::setprecision(4) << "  [1C. Proactive Idle Parking Relocation Bound (if nIdle>=3 & ql==0)]\n"
                  << "      Best Target=" << (best_rst >= 0 ? cd.depts[best_rst] : "none")
                  << ": Cost=" << best_reloc.cost << " (d=" << (best_reloc.cost - r0.cost)
                  << ", W=" << best_reloc.w << ", U95=" << best_reloc.u95 << ")\n";

        // 1D. Worst-10 vs Best-10 Seed Anatomy
        std::vector<int> s_ord(N_SEEDS);
        std::iota(s_ord.begin(), s_ord.end(), 0);
        std::sort(s_ord.begin(), s_ord.end(), [&](int a, int b) { return exact_outs[a].cost_seed < exact_outs[b].cost_seed; });
        auto avg_grp = [&](int st, int en) {
            double c_s = 0, u_s = 0, a_s = 0, b_s = 0, q_s = 0;
            for (int k = st; k < en; ++k) {
                const auto& o = exact_outs[s_ord[k]];
                c_s += o.cost_seed; u_s += o.u95_seed; a_s += o.n_arr_warm; b_s += o.max_burst_5m; q_s += o.max_q;
            }
            int d = en - st;
            return std::tuple{c_s/d, u_s/d, a_s/d, b_s/d, q_s/d};
        };
        auto [bc, bu, ba, bb, bq] = avg_grp(0, 10);
        auto [wc, wu, wa, wb, wq] = avg_grp(N_SEEDS - 10, N_SEEDS);
        std::cout << "  [1D. Best-10 vs Worst-10 Seed Anatomy]\n"
                  << "      Best-10 : Cost=" << std::setprecision(2) << bc << ", U95=" << std::setprecision(3) << bu
                  << ", WarmOrders=" << std::setprecision(1) << ba << ", Max5mBurst=" << bb << ", MaxQ=" << bq << "\n"
                  << "      Worst-10: Cost=" << std::setprecision(2) << wc << ", U95=" << std::setprecision(3) << wu
                  << ", WarmOrders=" << std::setprecision(1) << wa << ", Max5mBurst=" << wb << ", MaxQ=" << wq << "\n\n"
                  << std::setprecision(4);
    }
    return 0;
}
