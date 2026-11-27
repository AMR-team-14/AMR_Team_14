#include "../src/common/io.hpp"
#include <iomanip>
#include <map>

// Deep Physics Lab 3 (on current v3 ans_c1..c5.csv):
// Exp 3A: Case 2 Right-Tail Cliff Anatomy (Why do ~15% of seeds jump from U95=1.21 to 2.1~3.0m?)
// Exp 3B: Fleet Marginal Value Decomposition (n*-2 .. n*+2: +15 AGV cost vs d(cW*lam*W) vs d(cU*U95))
// Exp 3C: Tree Node-by-Node Prefix Pruning / Ablation Value (How much does each node actually save?)

struct JSimRoute { int nl = 0; std::vector<int> jn; std::vector<double> la; double total = 0.0; };
struct JSimCase { int nd = 0, nJ = 0; std::vector<int> JIDX; double RATE = 0, FAC = 1.2, MEAN = 1.0, LAM = 0; };
struct JSimDir { std::string dir; std::vector<std::vector<double>> DIST; std::vector<std::vector<JSimRoute>> ROUTE; };
struct JSimArrival { int qo, qd; double qt; int qi; };
struct JSimStream { int seed = 0; std::vector<JSimArrival> arr; };

static JSimCase build_jsim_case(const CaseData& cd) {
    JSimCase jc; jc.nd = cd.n_stations;
    std::vector<int> deg(cd.n_nodes, 0);
    for (const auto& e : cd.edges) { deg[e.u]++; deg[e.v]++; }
    jc.JIDX.assign(cd.n_nodes, -1); jc.nJ = 0;
    for (int v = 0; v < cd.n_nodes; ++v) if (deg[v] >= 3) jc.JIDX[v] = jc.nJ++;
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

struct SimOut3 {
    bool dq = false;
    double sum_w = 0.0, u95_seed = 0.0, cost_seed = 0.0;
    int cnt_w = 0, urg_cnt = 0, urg_s6_s4 = 0, urg_far = 0;
    std::vector<double> wu;
};

static SimOut3 run_sim_3(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                         const JSimStream& sp, int n, const Tree& tree)
{
    SimOut3 out;
    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest;
    const double T = cd.t_end, warm = cd.warm, FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC, pf = cd.p_occ * FAC;
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
    int qo_[512], qd_[512], qi_[512], ql=0, nIdle=n, nWq=0;
    double qt_[512], t=0.0, key[16], bk[16];
    int idle[64], qcnt[32];
    const int n_arr = (int)sp.arr.size(); int aidx = 0;

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
            if (vk[i] < rt->nl) { vst[i]=WQ; tact[i]=INF; int j=rt->jn[vk[i]]; jq[j*64+(jt[j]++&63)]=i; nWq++; return; }
            vnode[i] = vto[i];
            if (vph[i] == 0) {
                double w = t - vrtm[i];
                if (vrtm[i] >= warm) {
                    out.sum_w += w; out.cnt_w++;
                    if (vrd[i] == UD) {
                        out.wu.push_back(w);
                        out.urg_cnt++;
                        if (jd.DIST[vfrom[i]][vro[i]] > 80.0) out.urg_s6_s4++;
                        if (w >= 2.0) out.urg_far++;
                    }
                }
                vst[i] = LD; tact[i] = t + tlf;
            } else { vst[i] = UL; tact[i] = t + tuf; }
        } else if (st == CRS) { jbusy[vrt[i]->jn[vk[i]]]=-1; vk[i]++; startLeg(i); }
        else if (st == LD) travel(i, 1, vrd[i], cd.vc);
        else if (st == UL) { vst[i]=IDLE; tact[i]=INF; nIdle++; }
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
            if (nWq>0) for (int g=0; g<nJ; ++g) { if (jbusy[g]>=0||jh[g]==jt[g]) continue; int i=jq[g*64+(jh[g]++&63)]; vst[i]=CRS; tact[i]=t+pf; jbusy[g]=i; nWq--; }
            dispatch();
            bool any=false; for (int i=0; i<n; ++i) if (tact[i]<=t) { any=true; break; }
            if (!any) break;
        }
        if (ql>cd.qcap) { out.dq=true; return out; }
    }
    if (ql > cd.end_qmax) out.dq = true;
    if (!out.dq && out.cnt_w > 0 && !out.wu.empty()) {
        std::vector<double> swu = out.wu;
        std::sort(swu.begin(), swu.end());
        out.u95_seed = swu[(size_t)std::floor(0.95 * (swu.size() - 1))];
        out.cost_seed = cd.c_agv * n + cd.c_w * jc.LAM * (out.sum_w / out.cnt_w) + cd.c_u * out.u95_seed;
    }
    return out;
}

struct P3 { double cost = 999999, w = 999, u95 = 999; int dq = 0; };
static P3 eval_p3(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                  const std::vector<JSimStream>& sps, int n, const Tree& tr, std::vector<SimOut3>* raw = nullptr)
{
    int S = (int)sps.size();
    std::vector<SimOut3> outs(S);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < S; ++i) outs[i] = run_sim_3(cd, jc, jd, sps[i], n, tr);
    P3 res; double tw = 0; long long tc = 0; std::vector<double> wu; wu.reserve(S * 120);
    for (int i = 0; i < S; ++i) {
        if (outs[i].dq) res.dq++;
        tw += outs[i].sum_w; tc += outs[i].cnt_w;
        wu.insert(wu.end(), outs[i].wu.begin(), outs[i].wu.end());
    }
    if (raw) *raw = std::move(outs);
    if (res.dq == 0 && tc > 0 && !wu.empty()) {
        std::sort(wu.begin(), wu.end());
        res.w = tw / tc;
        res.u95 = wu[(size_t)std::floor(0.95 * (wu.size() - 1))];
        res.cost = cd.c_agv * n + cd.c_w * jc.LAM * res.w + cd.c_u * res.u95;
    }
    return res;
}

int main() {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "========================================================================\n";
    std::cout << "DEEP PHYSICS LAB 3 (C2 Cliff, Fleet Marginal Curve, Tree Node Ablation)\n";
    std::cout << "========================================================================\n\n";

    // Exp 3A: Case 2 Right-Tail Cliff (500 seeds)
    {
        CaseData cd = load_case("data/c2.json", "c2");
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c2.csv");
        JSimDir jd = compile_jsim_dir(cd, jc, ans_dir);
        std::vector<JSimStream> sp500(500);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < 500; ++i) sp500[i] = generate_jsim_stream(cd, jc, i + 1);
        std::vector<SimOut3> outs;
        auto p = eval_p3(cd, jc, jd, sp500, ans_n, ans_tree, &outs);
        int cliff_seeds = 0;
        double norm_u_tot = 0, norm_far = 0, cliff_u_tot = 0, cliff_far = 0;
        for (const auto& o : outs) {
            if (o.u95_seed >= 2.0) {
                cliff_seeds++;
                cliff_u_tot += o.urg_cnt; cliff_far += o.urg_s6_s4;
            } else {
                norm_u_tot += o.urg_cnt; norm_far += o.urg_s6_s4;
            }
        }
        std::cout << ">>> EXP 3A: CASE 2 BIMODAL U95 CLIFF (500 seeds, n=" << ans_n << ", Cost=" << p.cost << ") <<<\n"
                  << "  Cliff seeds (SeedU95 >= 2.0m): " << cliff_seeds << "/500 (" << std::setprecision(1) << (100.0 * cliff_seeds / 500.0) << "%)\n"
                  << "  Normal seeds avg urg_cnt=" << (norm_u_tot / (500 - cliff_seeds)) << ", avg far(de>80m) pickups=" << std::setprecision(2) << (norm_far / (500 - cliff_seeds))
                  << " (" << (100.0 * norm_far / std::max(1.0, norm_u_tot)) << "%)\n"
                  << "  Cliff  seeds avg urg_cnt=" << std::setprecision(1) << (cliff_u_tot / std::max(1, cliff_seeds)) << ", avg far(de>80m) pickups=" << std::setprecision(2) << (cliff_far / std::max(1, cliff_seeds))
                  << " (" << (100.0 * cliff_far / std::max(1.0, cliff_u_tot)) << "%)\n\n" << std::setprecision(4);
    }

    // Exp 3B & 3C for Cases 1..5 (200 seeds)
    for (int c = 1; c <= 5; ++c) {
        CaseData cd = load_case("data/c" + std::to_string(c) + ".json", "c" + std::to_string(c));
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c" + std::to_string(c) + ".csv");
        JSimDir jd = compile_jsim_dir(cd, jc, ans_dir);
        std::vector<JSimStream> sp200(200);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < 200; ++i) sp200[i] = generate_jsim_stream(cd, jc, i + 1);

        std::cout << ">>> CASE " << c << " (Base n=" << ans_n << ", " << ans_tree.n_nodes << "n) <<<\n";
        std::cout << "  [3B. Fleet Marginal Decomposition (200 seeds)]\n";
        for (int nf = std::max(cd.n_min, ans_n - 2); nf <= std::min(cd.n_max, ans_n + 2); ++nf) {
            auto r = eval_p3(cd, jc, jd, sp200, nf, ans_tree);
            if (r.dq > 0) {
                std::cout << "      n=" << nf << ": DQ(" << r.dq << "/200)\n";
            } else {
                double agv_c = cd.c_agv * nf;
                double w_c = cd.c_w * jc.LAM * r.w;
                double u_c = cd.c_u * r.u95;
                std::cout << "      n=" << nf << (nf == ans_n ? "* " : "  ")
                          << ": Total=" << r.cost << " = AGV(" << std::setprecision(1) << agv_c
                          << ") + W(" << w_c << ", " << std::setprecision(4) << r.w
                          << "m) + U95(" << std::setprecision(1) << u_c << ", " << std::setprecision(4) << r.u95 << "m)\n";
            }
        }

        std::cout << "  [3C. Tree Prefix Ablation Value (1n -> Full " << ans_tree.n_nodes << "n, 200 seeds)]\n";
        // Pure STTF 1-node (`de,min,50`) baseline for comparison
        Tree t_sttf; t_sttf.n_nodes = 1; t_sttf.nodes[1] = {2, true, 50.0, 0, 0};
        auto r_sttf = eval_p3(cd, jc, jd, sp200, ans_n, t_sttf);
        std::cout << "      STTF(1n): " << r_sttf.cost << " (W=" << r_sttf.w << ", U95=" << r_sttf.u95 << ")\n";
        for (int k = 1; k <= ans_tree.n_nodes; ++k) {
            Tree sub = ans_tree;
            sub.n_nodes = k;
            for (int i = 1; i <= k; ++i) {
                if (sub.nodes[i].lo > k) sub.nodes[i].lo = 0;
                if (sub.nodes[i].hi > k) sub.nodes[i].hi = 0;
            }
            auto rk = eval_p3(cd, jc, jd, sp200, ans_n, sub);
            std::cout << "      Prefix " << k << "n (+" << FEAT_NAMES[ans_tree.nodes[k].feat] << ","
                      << (ans_tree.nodes[k].is_min ? "min" : "max") << "," << ans_tree.nodes[k].th << "): "
                      << rk.cost << " (W=" << rk.w << ", U95=" << rk.u95 << ")\n";
        }
        std::cout << "\n";
    }
    return 0;
}
