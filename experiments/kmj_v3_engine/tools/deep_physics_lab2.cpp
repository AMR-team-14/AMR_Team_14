#include "../src/common/io.hpp"
#include <iomanip>
#include <map>

// Deep Physics Lab 2 (on current v3 ans_c1..c5.csv):
// Exp 2A: Case 4 Equal-Distance Tie-Breaking Audit (Current u<prev[v] vs Min-Junctions vs Avoid-Hot-Junc)
// Exp 2B: Stress-Test Stability Margin (alpha x 1.00, 1.05, 1.10, 1.15, 1.20 on 100 seeds)
// Exp 2C: Information-Bottleneck Feature Value (What if tree knew Route Junction Count `nl` or Junction-Penalty Time?)
// Exp 2D: 500-Seed U95 Bimodal Cliff & Seed-to-Seed Variance Audit

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

// tie_mode: 0 = exact sim (FW k-order), 1 = min-junctions on equal dist, 2 = max-junctions (worst tie)
static JSimDir compile_jsim_dir_tie(const CaseData& cd, const JSimCase& jc, const std::string& bits, int tie_mode) {
    JSimDir jd; jd.dir = bits;
    const int V = cd.n_nodes;
    if (tie_mode == 0) {
        const double INF = 1e18;
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

    // Dijkstra minimizing/maximizing junctions among equal-distance shortest paths
    std::vector<std::vector<std::pair<int, double>>> adj(V);
    for (int k = 0; k < cd.n_edges; ++k) {
        int u = cd.edges[k].u, v = cd.edges[k].v;
        if (bits[k] == '1') std::swap(u, v);
        adj[u].push_back({v, cd.edges[k].dist});
    }
    jd.DIST.assign(jc.nd, std::vector<double>(jc.nd, 0.0));
    jd.ROUTE.assign(jc.nd, std::vector<JSimRoute>(jc.nd));
    for (int a = 0; a < jc.nd; ++a) {
        int pa = cd.pd[a];
        std::vector<double> dist(V, 1e18);
        std::vector<int> jcnt(V, (tie_mode == 1) ? 999999 : -1), par(V, -1);
        dist[pa] = 0.0; jcnt[pa] = 0;
        using S = std::tuple<double, int, int>;
        std::priority_queue<S, std::vector<S>, std::greater<S>> pq;
        pq.push({0.0, 0, pa});
        while (!pq.empty()) {
            auto [d, jc_u, u] = pq.top(); pq.pop();
            int real_jc = (tie_mode == 1) ? jc_u : -jc_u;
            if (d > dist[u] + 1e-9 || (std::abs(d - dist[u]) <= 1e-9 && real_jc != jcnt[u])) continue;
            for (auto [v, w] : adj[u]) {
                double ndist = d + w;
                int nj = real_jc + (jc.JIDX[v] >= 0 ? 1 : 0);
                bool imp = (ndist < dist[v] - 1e-9) ||
                           (std::abs(ndist - dist[v]) <= 1e-9 &&
                            ((tie_mode == 1) ? (nj < jcnt[v]) : (nj > jcnt[v])));
                if (imp) {
                    dist[v] = ndist; jcnt[v] = nj; par[v] = u;
                    pq.push({ndist, (tie_mode == 1) ? nj : -nj, v});
                }
            }
        }
        for (int b = 0; b < jc.nd; ++b) {
            int pb = cd.pd[b];
            jd.DIST[a][b] = dist[pb];
            std::vector<int> path;
            for (int c = pb; c != -1; c = par[c]) path.push_back(c);
            std::reverse(path.begin(), path.end());
            JSimRoute rt; rt.total = dist[pb];
            for (size_t i = 1; i + 1 < path.size(); ++i) {
                int c = path[i];
                if (jc.JIDX[c] >= 0) { rt.jn.push_back(jc.JIDX[c]); rt.la.push_back(dist[c]); }
            }
            rt.nl = (int)rt.jn.size();
            jd.ROUTE[a][b] = std::move(rt);
        }
    }
    return jd;
}

static JSimStream generate_jsim_stream(const CaseData& cd, const JSimCase& jc, int seed, double alpha_mult = 1.0) {
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
    double mean_scaled = jc.MEAN / alpha_mult;
    double next = EXP() * mean_scaled; int serial = 0;
    while (next <= cd.t_end) {
        double u = R() * jc.RATE; int k = 0;
        while (k < cd.n_flows - 1 && u >= cd.flows[k].rate) { u -= cd.flows[k].rate; ++k; }
        sp.arr.push_back({cd.flows[k].o_st, cd.flows[k].d_st, next, serial++});
        next += EXP() * mean_scaled;
    }
    return sp;
}

struct SimOut2 {
    bool dq = false;
    double sum_w = 0.0, u95_seed = 0.0, cost_seed = 0.0;
    int cnt_w = 0, max_q = 0, end_q = 0;
    std::vector<double> wu;
};

// feat_mode: 0=exact tree, 1=add 0.36t-aware empty travel time (`de + 18*nl` in tie-break/de)
static SimOut2 run_sim_2(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                         const JSimStream& sp, int n, const Tree& tree, int feat_mode = 0, double alpha_mult = 1.0)
{
    SimOut2 out;
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
    int qo_[512], qd_[512], qi_[512], ql=0, nIdle=n, nWq=0, maxQ=0;
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
                if (vrtm[i] >= warm) { out.sum_w += w; out.cnt_w++; if (vrd[i]==UD) out.wu.push_back(w); }
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
                    int ro=qo_[b];
                    double de = dist_vn[ro];
                    double eff_de = (feat_mode == 1) ? (de + 18.0 * jd.ROUTE[vn][ro].nl) : de;
                    int kl=0, id=1;
                    for (int g=0; g<8 && id!=0; ++g) {
                        const TreeNode& tn=tree.nodes[id]; int ff=tn.feat, side;
                        if (ff==0) side=(qd_[b]==UD)?1:0;
                        else if (ff==1) side=((t-qt_[b])<=tn.th)?0:1;
                        else { double val=(ff==2)?eff_de:(ff==3)?jd.DIST[ro][qd_[b]]:(double)qcnt[ro]; side=(val<=tn.th)?0:1; }
                        key[kl++]=tn.is_min?(double)side:(double)(1-side);
                        id=side?tn.hi:tn.lo;
                    }
                    key[kl++]=eff_de; key[kl++]=qt_[b]; key[kl++]=(double)qi_[b]; key[kl++]=(double)vi;
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
        maxQ = std::max(maxQ, ql);
        if (ql>cd.qcap) { out.dq=true; out.max_q=maxQ; return out; }
    }
    out.max_q = maxQ; out.end_q = ql;
    if (ql > cd.end_qmax) out.dq = true;
    if (!out.dq && out.cnt_w > 0 && !out.wu.empty()) {
        std::vector<double> swu = out.wu;
        std::sort(swu.begin(), swu.end());
        out.u95_seed = swu[(size_t)std::floor(0.95 * (swu.size() - 1))];
        out.cost_seed = cd.c_agv * n + cd.c_w * (jc.LAM * alpha_mult) * (out.sum_w / out.cnt_w) + cd.c_u * out.u95_seed;
    }
    return out;
}

struct P2 { double cost = 999999, w = 999, u95 = 999; int dq = 0, max_end_q = 0; };
static P2 eval_p2(const CaseData& cd, const JSimCase& jc, const JSimDir& jd,
                  const std::vector<JSimStream>& sps, int n, const Tree& tr, int feat_mode = 0, double am = 1.0,
                  std::vector<SimOut2>* raw = nullptr)
{
    int S = (int)sps.size();
    std::vector<SimOut2> outs(S);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < S; ++i) outs[i] = run_sim_2(cd, jc, jd, sps[i], n, tr, feat_mode, am);
    P2 res; double tw = 0; long long tc = 0; std::vector<double> wu; wu.reserve(S * 120);
    for (int i = 0; i < S; ++i) {
        res.max_end_q = std::max(res.max_end_q, outs[i].end_q);
        if (outs[i].dq) res.dq++;
        tw += outs[i].sum_w; tc += outs[i].cnt_w;
        wu.insert(wu.end(), outs[i].wu.begin(), outs[i].wu.end());
    }
    if (raw) *raw = std::move(outs);
    if (res.dq == 0 && tc > 0 && !wu.empty()) {
        std::sort(wu.begin(), wu.end());
        res.w = tw / tc;
        res.u95 = wu[(size_t)std::floor(0.95 * (wu.size() - 1))];
        res.cost = cd.c_agv * n + cd.c_w * (jc.LAM * am) * res.w + cd.c_u * res.u95;
    }
    return res;
}

int main() {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "========================================================================\n";
    std::cout << "DEEP PHYSICS LAB 2 (Tie-Breaks, Stress Margin, nl-Aware, 500-Seed Cliffs)\n";
    std::cout << "========================================================================\n\n";

    for (int c = 1; c <= 5; ++c) {
        CaseData cd = load_case("data/c" + std::to_string(c) + ".json", "c" + std::to_string(c));
        JSimCase jc = build_jsim_case(cd);
        auto [ans_dir, ans_n, ans_tree] = load_solution_csv("answers/ans_c" + std::to_string(c) + ".csv");
        JSimDir jd0 = compile_jsim_dir_tie(cd, jc, ans_dir, 0);

        std::vector<JSimStream> sp500(500), sp100(100);
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < 500; ++i) sp500[i] = generate_jsim_stream(cd, jc, i + 1, 1.0);
        for (int i = 0; i < 100; ++i) sp100[i] = sp500[i];

        std::vector<SimOut2> outs500;
        auto base500 = eval_p2(cd, jc, jd0, sp500, ans_n, ans_tree, 0, 1.0, &outs500);
        std::cout << ">>> CASE " << c << " (n=" << ans_n << ", 500-seed=" << base500.cost
                  << ", W=" << base500.w << ", U95=" << base500.u95 << ") <<<\n";

        // 2A. Equal-Distance Tie-Break Counterfactual (especially Case 4!)
        if (c == 1 || c == 4) {
            JSimDir jd_minj = compile_jsim_dir_tie(cd, jc, ans_dir, 1);
            JSimDir jd_maxj = compile_jsim_dir_tie(cd, jc, ans_dir, 2);
            auto r_minj = eval_p2(cd, jc, jd_minj, sp100, ans_n, ans_tree, 0, 1.0);
            auto r_maxj = eval_p2(cd, jc, jd_maxj, sp100, ans_n, ans_tree, 0, 1.0);
            auto r_100 = eval_p2(cd, jc, jd0, sp100, ans_n, ans_tree, 0, 1.0);
            std::cout << "  [2A. Shortest-Path Tie-Break Sensitivity (100 seeds)]\n"
                      << "      Exact FW=" << r_100.cost << " | Min-Junctions=" << r_minj.cost
                      << " (d=" << (r_minj.cost - r_100.cost) << ") | Max-Junctions=" << r_maxj.cost
                      << " (d=" << (r_maxj.cost - r_100.cost) << ")\n";
        }

        // 2B. Stress-Test Demand Elasticity (alpha x 1.00 .. 1.25 on 100 seeds)
        std::cout << "  [2B. Demand Surge Stress Test (alpha multiplier, 100 seeds)]\n      ";
        for (double am : {1.00, 1.05, 1.10, 1.15, 1.20, 1.25}) {
            std::vector<JSimStream> sps_a(100);
            #pragma omp parallel for schedule(static)
            for (int i = 0; i < 100; ++i) sps_a[i] = generate_jsim_stream(cd, jc, i + 1, am);
            auto ra = eval_p2(cd, jc, jd0, sps_a, ans_n, ans_tree, 0, am);
            std::cout << "x" << std::setprecision(2) << am << ":";
            if (ra.dq > 0) std::cout << "DQ(" << ra.dq << "/100)  ";
            else std::cout << std::setprecision(1) << ra.cost << "(eQ=" << ra.max_end_q << ")  ";
        }
        std::cout << "\n" << std::setprecision(4);

        // 2C. Feature Information Value: What if distance `de` included `0.36t` junction cost (`de + 18*nl`)?
        auto r_base100 = eval_p2(cd, jc, jd0, sp100, ans_n, ans_tree, 0, 1.0);
        auto r_nlaware = eval_p2(cd, jc, jd0, sp100, ans_n, ans_tree, 1, 1.0);
        std::cout << "  [2C. 0.36t-Aware Effective Distance (de + 18*nl) vs Pure `de` (100 seeds)]\n"
                  << "      Pure `de`=" << r_base100.cost << " | `de+18*nl`=" << r_nlaware.cost
                  << " (d=" << (r_nlaware.cost - r_base100.cost) << ", W=" << r_nlaware.w << ", U95=" << r_nlaware.u95 << ")\n";

        // 2D. 500-Seed Distribution & Cliff Jump Analysis
        std::vector<double> scs, su95;
        for (const auto& o : outs500) { scs.push_back(o.cost_seed); su95.push_back(o.u95_seed); }
        std::sort(scs.begin(), scs.end());
        std::sort(su95.begin(), su95.end());
        double mean_s = std::accumulate(scs.begin(), scs.end(), 0.0) / 500.0;
        double var_s = 0.0;
        for (double x : scs) var_s += (x - mean_s) * (x - mean_s);
        double std_s = std::sqrt(var_s / 500.0);
        std::cout << "  [2D. 500-Seed Individual Seed Distribution]\n"
                  << "      SeedCost Mean=" << std::setprecision(2) << mean_s << ", Std=" << std_s
                  << " | P05=" << scs[25] << ", P50=" << scs[250] << ", P95=" << scs[475] << ", Max=" << scs[499] << "\n"
                  << "      SeedU95  P05=" << std::setprecision(3) << su95[25] << ", P50=" << su95[250]
                  << ", P90=" << su95[450] << ", P95=" << su95[475] << ", Max=" << su95[499] << "\n\n"
                  << std::setprecision(4);
    }
    return 0;
}
