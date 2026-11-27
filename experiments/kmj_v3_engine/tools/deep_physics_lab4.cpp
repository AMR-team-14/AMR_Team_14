#include "../src/common/io.hpp"
#include <iomanip>
#include <queue>

// Deep Physics Lab 4 (100 seeds on current v3 ans_c1..c5.csv):
// Exp 4A: Time-Window Stationarity Audit (300..1200 vs 1200..2100 vs 2100..3000) -> Is queue drifting?
// Exp 4B: All Legal 1-Corridor Flips (C2..C5) -> Map (d_sumFD, d_maxRho, d_Cost, d_W, d_U95)
// Exp 4C: Analytical Flow (F_AVG, F_URG) vs Actual 100-Seed DES Cost Correlation across Valid SCCs (C3=54, C5=324)

struct JSimRoute { int nl = 0; std::vector<int> jn; std::vector<double> la; double total = 0.0; };
struct JSimCase { int nd = 0, nJ = 0; std::vector<int> JIDX; double RATE = 0, FAC = 1.2, MEAN = 1.0, LAM = 0; };
struct JSimDir { std::string dir; double sum_fd = 0; std::vector<std::vector<double>> DIST; std::vector<std::vector<JSimRoute>> ROUTE; };
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
    jd.sum_fd = 0;
    for (const auto& f : cd.flows) jd.sum_fd += f.rate * jd.DIST[f.o_st][f.d_st];
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

struct SimOut4 {
    bool dq = false;
    double sum_w = 0.0; int cnt_w = 0; std::vector<double> wu;
    double sw_win[3]{}; int cw_win[3]{}; std::vector<double> wu_win[3];
};

static SimOut4 run_sim_4(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                         const JSimStream& sp, int n, const Tree& tree)
{
    SimOut4 out;
    constexpr int IDLE = 0, DRV = 1, WQ = 2, CRS = 3, LD = 4, UL = 5;
    const int nd = jc.nd, nJ = jc.nJ, UD = cd.urgent_dest;
    const double T = cd.t_end, warm = cd.warm, FAC = jc.FAC;
    const double tlf = cd.tl * FAC, tuf = cd.tu * FAC, pf = cd.p_occ * FAC;
    const double INF = std::numeric_limits<double>::infinity();

    int vnode[64], vst[64], vph[64], vto[64], vk[64], vro[64], vrd[64];
    double vspd[64], vpos[64], vd1[64], tact[64], vrtm[64];
    const JSimRoute* vrt[64];
    for (int i = 0; i < n; ++i) {
        vnode[i]=i%nd; vst[i]=IDLE; vph[i]=0; vto[i]=0; vk[i]=0; vro[i]=0; vrd[i]=0;
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
                    out.sum_w += w; out.cnt_w++; if (vrd[i]==UD) out.wu.push_back(w);
                    int wi = std::clamp((int)((vrtm[i] - warm) / 900.0), 0, 2);
                    out.sw_win[wi] += w; out.cw_win[wi]++; if (vrd[i]==UD) out.wu_win[wi].push_back(w);
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
            vro[bv]=qo_[bb]; vrd[bv]=qd_[bb]; vrtm[bv]=qt_[bb];
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
    return out;
}

static std::pair<double, double> eval_f_avg_and_rho(const CaseData& cd, const JSimCase& jc, const JSimDir& jd) {
    const int nd = jc.nd, nJ = jc.nJ;
    const double t_inter = cd.p_occ * jc.FAC, fac_e = jc.FAC / cd.ve, fac_l = jc.FAC / cd.vc;
    std::vector<double> junc_cross_hr(nJ, 0.0), sup(nd, 0.0);
    double t_load_hr = 0.0;
    for (const auto& f : cd.flows) {
        double r = cd.alpha * f.rate;
        const auto& rt = jd.ROUTE[f.o_st][f.d_st];
        t_load_hr += r * (rt.total * fac_l + rt.nl * t_inter);
        for (int j : rt.jn) junc_cross_hr[j] += r;
        sup[f.d_st] += r; sup[f.o_st] -= r;
    }
    const int S = nd, T_node = nd + 1, NV = nd + 2;
    struct E { int to, rev; double cap, cost; };
    std::vector<std::vector<E>> g(NV);
    auto add_e = [&](int u, int v, double cap, double cost) {
        g[u].push_back({v, (int)g[v].size(), cap, cost});
        g[v].push_back({u, (int)g[u].size() - 1, 0.0, -cost});
    };
    for (int s = 0; s < nd; ++s) {
        if (sup[s] > 1e-9) add_e(S, s, sup[s], 0.0);
        else if (sup[s] < -1e-9) add_e(s, T_node, -sup[s], 0.0);
    }
    std::vector<std::pair<int,int>> links;
    for (int a = 0; a < nd; ++a) if (sup[a] > 1e-9)
        for (int b = 0; b < nd; ++b) if (sup[b] < -1e-9) {
            links.push_back({a, (int)g[a].size()});
            add_e(a, b, 1e9, jd.DIST[a][b] * fac_e + jd.ROUTE[a][b].nl * t_inter);
        }
    double t_emp = 0.0;
    while (true) {
        std::vector<double> d(NV, 1e18); std::vector<int> pv(NV,-1), pe(NV,-1), inq(NV,0);
        std::queue<int> q; d[S]=0; q.push(S); inq[S]=1;
        while (!q.empty()) {
            int u=q.front(); q.pop(); inq[u]=0;
            for (int i=0;i<(int)g[u].size();++i) {
                const auto& e=g[u][i];
                if (e.cap>1e-9 && d[u]+e.cost+1e-12<d[e.to]) {
                    d[e.to]=d[u]+e.cost; pv[e.to]=u; pe[e.to]=i;
                    if (!inq[e.to]) { q.push(e.to); inq[e.to]=1; }
                }
            }
        }
        if (pv[T_node]<0) break;
        double p=1e18; for (int v=T_node;v!=S;v=pv[v]) p=std::min(p,g[pv[v]][pe[v]].cap);
        for (int v=T_node;v!=S;v=pv[v]) { auto& e=g[pv[v]][pe[v]]; e.cap-=p; g[v][e.rev].cap+=p; }
        t_emp += p * d[T_node];
    }
    for (auto [a, idx] : links) {
        double u = 1e9 - g[a][idx].cap;
        if (u > 1e-9) for (int j : jd.ROUTE[a][g[a][idx].to].jn) junc_cross_hr[j] += u;
    }
    double wq = 0, mrho = 0;
    for (int j = 0; j < nJ; ++j) {
        double rho = junc_cross_hr[j] * t_inter / 60.0;
        mrho = std::max(mrho, rho);
        double rc = std::min(0.95, rho);
        wq += junc_cross_hr[j] * (0.5 * t_inter * rc / (1.0 - rc));
    }
    return {(t_load_hr + t_emp + 1.8 * wq) / 60.0, mrho};
}

int main() {
    std::cout << std::fixed << std::setprecision(4);
    const int N_SEEDS = 100;
    std::cout << "========================================================================\n";
    std::cout << "DEEP PHYSICS LAB 4 (Stationarity Drift & Legal 1-Corridor Sensitivity)\n";
    std::cout << "========================================================================\n\n";

    for (int c = 1; c <= 5; ++c) {
        CaseData cd = load_case("data/c" + std::to_string(c) + ".json", "c" + std::to_string(c));
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c" + std::to_string(c) + ".csv");
        JSimDir jd0 = compile_jsim_dir(cd, jc, ans_dir);
        std::vector<JSimStream> sps(N_SEEDS);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < N_SEEDS; ++i) sps[i] = generate_jsim_stream(cd, jc, i + 1);

        std::vector<SimOut4> outs(N_SEEDS);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < N_SEEDS; ++i) outs[i] = run_sim_4(cd, jc, jd0, sps[i], ans_n, ans_tree);

        double sw[3] = {0}, tw = 0; int cw[3] = {0}, tc = 0;
        std::vector<double> wu[3], all_wu;
        for (const auto& o : outs) {
            tw += o.sum_w; tc += o.cnt_w; all_wu.insert(all_wu.end(), o.wu.begin(), o.wu.end());
            for (int w = 0; w < 3; ++w) { sw[w] += o.sw_win[w]; cw[w] += o.cw_win[w]; wu[w].insert(wu[w].end(), o.wu_win[w].begin(), o.wu_win[w].end()); }
        }
        std::sort(all_wu.begin(), all_wu.end());
        double base_w = tw / tc, base_u = all_wu[(size_t)std::floor(0.95 * (all_wu.size() - 1))];
        double base_cost = cd.c_agv * ans_n + cd.c_w * jc.LAM * base_w + cd.c_u * base_u;

        std::cout << ">>> CASE " << c << " (n=" << ans_n << ", 100-seed=" << base_cost << ") <<<\n";
        std::cout << "  [4A. Time-Window Stationarity (Early 300-1200 | Mid 1200-2100 | Late 2100-3000)]\n      ";
        const char* wlbl[3] = {"Early", "Mid  ", "Late "};
        for (int w = 0; w < 3; ++w) {
            std::sort(wu[w].begin(), wu[w].end());
            double ww = sw[w] / cw[w], uu = wu[w][(size_t)std::floor(0.95 * (wu[w].size() - 1))];
            std::cout << wlbl[w] << ": W=" << std::setprecision(3) << ww << "m, U95=" << uu << "m  |  ";
        }
        std::cout << "\n" << std::setprecision(4);

        if (cd.fixed_dir.empty()) {
            auto [bf, brho] = eval_f_avg_and_rho(cd, jc, jd0);
            struct FlipRes { int cid; double d_cost, d_sumfd, d_favg, d_rho; };
            std::vector<FlipRes> flips;
            for (int cid = 0; cid < (int)cd.corridors.size(); ++cid) {
                if (cd.corridors[cid].is_parallel_pair) continue;
                std::string ndir = ans_dir;
                for (int e : cd.corridors[cid].edges) ndir[e] = (ndir[e] == '0') ? '1' : '0';
                if (!is_valid_dir(cd, ndir)) continue;
                JSimDir jdf = compile_jsim_dir(cd, jc, ndir);
                auto [ff, frho] = eval_f_avg_and_rho(cd, jc, jdf);
                std::vector<SimOut4> fo(N_SEEDS); int dq = 0;
                #pragma omp parallel for schedule(dynamic, 1)
                for (int i = 0; i < N_SEEDS; ++i) fo[i] = run_sim_4(cd, jc, jdf, sps[i], ans_n, ans_tree);
                double ftw = 0; int ftc = 0; std::vector<double> fwu;
                for (const auto& o : fo) { if (o.dq) dq++; ftw += o.sum_w; ftc += o.cnt_w; fwu.insert(fwu.end(), o.wu.begin(), o.wu.end()); }
                double fc = 999999.0;
                if (dq == 0 && ftc > 0 && !fwu.empty()) {
                    std::sort(fwu.begin(), fwu.end());
                    fc = cd.c_agv * ans_n + cd.c_w * jc.LAM * (ftw / ftc) + cd.c_u * fwu[(size_t)std::floor(0.95 * (fwu.size() - 1))];
                }
                flips.push_back({cid, fc - base_cost, jdf.sum_fd - jd0.sum_fd, ff - bf, frho - brho});
            }
            std::sort(flips.begin(), flips.end(), [](const FlipRes& a, const FlipRes& b) { return a.d_cost < b.d_cost; });
            std::cout << "  [4B. All Legal 1-Corridor Flips (Total=" << flips.size() << ", base sumFD=" << std::setprecision(1) << jd0.sum_fd
                      << ", maxRho=" << std::setprecision(3) << brho << ")]\n";
            for (int k = 0; k < std::min(5, (int)flips.size()); ++k) {
                const auto& fr = flips[k];
                std::cout << "      #" << (k + 1) << " cid=" << fr.cid << ": dCost=" << std::setprecision(2) << std::showpos << fr.d_cost
                          << " | d_sumFD=" << fr.d_sumfd << " | d_fAVG=" << std::setprecision(3) << fr.d_favg
                          << " | d_maxRho=" << fr.d_rho << std::noshowpos << "\n";
            }
        }
        std::cout << "\n" << std::setprecision(4);
    }
    return 0;
}
