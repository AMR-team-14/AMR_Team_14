#include "search.hpp"
#include "simulator.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
std::string tree_key_str(const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ostringstream os;
    for (int i = 1; i <= t.n_nodes; ++i) {
        const auto& n = t.nodes[i];
        os << n.feat << (n.is_min ? 'm' : 'M') << std::llround(n.th * 100) << ':' << n.lo << ':' << n.hi << ';';
    }
    return os.str();
}

bool is_noop_attach(const Tree& t, int p_nid, bool is_hi, int feat, bool is_min) {
    int urg_state = -1;
    bool dl_gt_25 = false;
    auto dfs = [&](auto& self, int curr, int us, bool dgt) -> bool {
        if (curr == 0) return false;
        const auto& tn = t.nodes[curr];
        if (curr == p_nid) {
            if (tn.feat == 0) us = is_hi ? 1 : 0;
            else if (tn.feat == 3 && tn.th >= 25 && is_hi) dgt = true;
            urg_state = us; dl_gt_25 = dgt;
            return true;
        }
        int lo_u = (tn.feat == 0) ? 0 : us;
        int hi_u = (tn.feat == 0) ? 1 : us;
        bool hi_d = (tn.feat == 3 && tn.th >= 25) ? true : dgt;
        return self(self, tn.lo, lo_u, dgt) || self(self, tn.hi, hi_u, hi_d);
    };
    dfs(dfs, 1, -1, false);
    if (urg_state == 1) {
        if (feat == 3 || feat == 4) return true;
        if (feat == 1 && !is_min) return true;
        if (feat == 2 && is_min) return true;
    }
    if (dl_gt_25 && feat == 0) return true;
    return false;
}

struct Split { int feat; bool is_min; double th; };

// Screen single-node changes on 12 streams and evaluate up to 10 finalists on all 55.
Tree fast_robust_polish(const SimData& sd, int n_agv, Tree base_t, double& out_cost, int n_threads) {
    static const std::vector<double> DE_GRID = {0, 25, 65, 90, 115, 140, 155, 180};
    static const std::vector<double> DL_GRID = {25, 50, 65, 90};
    static const std::vector<double> QO_GRID = {1, 2, 3, 4, 5};
    static const std::vector<double> W_GRID = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0, 7.0, 8.0};

    int n_tot = (int)sd.streams.size(); // 55 streams
    Tree best_t = canonicalize_tree(base_t);
    double best_c = eval_streams(sd, n_agv, best_t, 0, n_tot);

    bool improved = true;
    while (improved) {
        improved = false;
        std::vector<Tree> cands;
        std::set<std::string> seen;
        seen.insert(tree_key_str(best_t));

        for (int nid = 1; nid <= best_t.n_nodes; ++nid) {
            const auto& tn = best_t.nodes[nid];
            if (tn.feat == 0) continue;
            std::vector<double> grid;
            if (tn.feat == 1) {
                grid = W_GRID;
                for (int d = -4; d <= 4; ++d) {
                    grid.push_back(std::round(std::max(0.1, tn.th + d * 0.1) * 100.0) / 100.0);
                }
                std::sort(grid.begin(), grid.end());
                grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
            } else if (tn.feat == 2) grid = DE_GRID;
            else if (tn.feat == 3) grid = DL_GRID;
            else grid = QO_GRID;

            for (bool cm : {true, false}) {
                for (double cth : grid) {
                    Tree cand = best_t;
                    cand.nodes[nid].is_min = cm; cand.nodes[nid].th = cth;
                    if (is_valid_tree(cand) && seen.insert(tree_key_str(cand)).second) cands.push_back(cand);
                }
            }
            if (tn.lo == 0 && tn.hi == 0 && nid != 1) {
                Tree cand = best_t;
                for (int pid = 1; pid <= cand.n_nodes; ++pid) {
                    if (cand.nodes[pid].lo == nid) cand.nodes[pid].lo = 0;
                    if (cand.nodes[pid].hi == nid) cand.nodes[pid].hi = 0;
                }
                cand = canonicalize_tree(cand);
                if (is_valid_tree(cand) && seen.insert(tree_key_str(cand)).second) cands.push_back(cand);
            }
        }
        if (cands.empty()) break;

        // Stage 1: Screen on 12 streams (seed 11 + unseen 101..111)
        std::vector<double> scr(cands.size(), 999999.0);
        {
            std::vector<std::thread> ths;
            for (int ti = 0; ti < n_threads; ++ti) {
                ths.emplace_back([&, ti]() {
                    for (size_t i = ti; i < cands.size(); i += n_threads) {
                        double c0 = run_sim(sd, 0, n_agv, cands[i], 65).cost;
                        if (c0 < 900000.0) {
                            double c11 = eval_streams(sd, n_agv, cands[i], 5, 16, 65);
                            if (c11 < 900000.0) scr[i] = (c0 + 11.0 * c11) / 12.0;
                        }
                    }
                });
            }
            for (auto& th : ths) th.join();
        }
        std::vector<int> idx(cands.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return scr[a] < scr[b]; });

        // Stage 2: Exact 55-stream eval on top 10
        int top_k = std::min((int)idx.size(), 10);
        std::vector<double> exact_c(top_k, 999999.0);
        {
            std::vector<std::thread> ths;
            for (int ti = 0; ti < n_threads; ++ti) {
                ths.emplace_back([&, ti]() {
                    for (int k = ti; k < top_k; k += n_threads) {
                        if (scr[idx[k]] < 900000.0) {
                            exact_c[k] = eval_streams(sd, n_agv, cands[idx[k]], 0, n_tot);
                        }
                    }
                });
            }
            for (auto& th : ths) th.join();
        }
        for (int k = 0; k < top_k; ++k) {
            if (exact_c[k] < best_c - 1e-4) {
                best_c = exact_c[k];
                best_t = cands[idx[k]];
                improved = true;
            }
        }
    }
    out_cost = best_c;
    return best_t;
}
} // namespace

void run_analysis(const SimData& sd, const std::string& csv_path, int n_threads) {
    auto [base_n, base_t] = load_csv_tree(sd, csv_path);
    std::cout << "=== FULL ANALYSIS FOR " << csv_path << " (n=" << base_n << ", nodes=" << base_t.n_nodes << ") ===\n";
    auto eval_50_unseen = [&](int n_agv, const Tree& tr) {
        int n_unseen = (int)sd.streams.size() - 5;
        std::vector<SimMetrics> ms(n_unseen);
        std::vector<std::thread> ths;
        for (int ti = 0; ti < n_threads; ++ti) {
            ths.emplace_back([&, ti]() {
                for (int i = ti; i < n_unseen; i += n_threads) ms[i] = run_sim(sd, 5 + i, n_agv, tr);
            });
        }
        for (auto& th : ths) th.join();
        return ms;
    };

    std::cout << "\n[1] Public 5-Seed Breakdown (Seeds 11..15):\n";
    double p_sum = 0, p_w = 0, p_u = 0; int p_maxq = 0, p_endq = 0;
    const int pub_seeds[5] = {11, 12, 13, 14, 15};
    for (int s = 0; s < 5; ++s) {
        SimMetrics m = run_sim(sd, s, base_n, base_t);
        p_sum += m.cost; p_w += m.w_bar; p_u += m.u_95;
        p_maxq = std::max(p_maxq, m.max_q); p_endq = std::max(p_endq, m.end_q);
        std::cout << "  Seed " << pub_seeds[s] << ": cost=" << std::fixed << std::setprecision(2) << m.cost
                  << " | w_bar=" << std::setprecision(4) << m.w_bar << " | u95=" << m.u_95
                  << " | maxQ=" << m.max_q << " | endQ=" << m.end_q << "\n";
    }
    std::cout << "  MEAN   : cost=" << std::setprecision(2) << p_sum / 5.0
              << " | agv=" << 15.0 * base_n << " | wait_cost=" << 102.24 * (p_w / 5.0)
              << " | urg_cost=" << 100.0 * (p_u / 5.0) << " | w_bar=" << std::setprecision(4) << p_w / 5.0
              << " | u95=" << p_u / 5.0 << " | maxQ=" << p_maxq << " | endQ=" << p_endq << "\n";

    std::cout << "\n[2] Fleet Size Sensitivity (n=16..23) on Public (5) & Unseen (50):\n";
    for (int na = 16; na <= 23; ++na) {
        double pm_c = 0, pm_w = 0, pm_u = 0; int pm_disq = 0, pm_mq = 0;
        for (int s = 0; s < 5; ++s) {
            SimMetrics m = run_sim(sd, s, na, base_t);
            if (m.cost >= 900000.0) ++pm_disq;
            else { pm_c += m.cost; pm_w += m.w_bar; pm_u += m.u_95; }
            pm_mq = std::max(pm_mq, m.max_q);
        }
        auto ums = eval_50_unseen(na, base_t);
        std::vector<double> ucs; int udisq = 0, umq = 0, ueq = 0;
        double uw = 0, uu = 0;
        for (const auto& m : ums) {
            umq = std::max(umq, m.max_q); ueq = std::max(ueq, m.end_q);
            if (m.cost >= 900000.0) ++udisq;
            else { ucs.push_back(m.cost); uw += m.w_bar; uu += m.u_95; }
        }
        std::sort(ucs.begin(), ucs.end());
        double umean = ucs.empty() ? 999999.0 : std::accumulate(ucs.begin(), ucs.end(), 0.0) / ucs.size();
        double uvar = 0.0; for (double v : ucs) uvar += (v - umean) * (v - umean);
        double ustd = ucs.size() > 1 ? std::sqrt(uvar / ucs.size()) : 0.0;
        double umax = ucs.empty() ? 999999.0 : ucs.back();
        std::cout << "  n=" << std::setw(2) << na << " | pub=" << std::setprecision(2) << (pm_disq ? 999999.0 : pm_c / 5.0)
                  << " (w=" << std::setprecision(4) << (pm_disq ? 999.0 : pm_w / 5.0) << ", u95=" << (pm_disq ? 999.0 : pm_u / 5.0)
                  << ", maxQ=" << pm_mq << ") | unseen50: mean=" << std::setprecision(2) << umean
                  << " (w=" << std::setprecision(4) << (ucs.empty() ? 999.0 : uw / ucs.size())
                  << ", u95=" << (ucs.empty() ? 999.0 : uu / ucs.size()) << ") std=" << std::setprecision(2) << ustd
                  << " max=" << umax << " maxQ=" << umq << " endQ=" << ueq << " disq=" << udisq << "/50\n";
    }

    std::cout << "\n[3] Node Ablation Analysis (Removing each node 2..N):\n";
    double full_pub = p_sum / 5.0;
    auto full_u50 = eval_50_unseen(base_n, base_t);
    double full_u_mean = 0; for (const auto& m : full_u50) full_u_mean += m.cost / full_u50.size();
    for (int rem = 2; rem <= base_t.n_nodes; ++rem) {
        Tree ab = base_t;
        for (int pid = 1; pid <= ab.n_nodes; ++pid) {
            if (ab.nodes[pid].lo == rem) ab.nodes[pid].lo = 0;
            if (ab.nodes[pid].hi == rem) ab.nodes[pid].hi = 0;
        }
        ab = canonicalize_tree(ab);
        double ac = 0, aw = 0, au = 0;
        for (int s = 0; s < 5; ++s) { SimMetrics m = run_sim(sd, s, base_n, ab); ac += m.cost; aw += m.w_bar; au += m.u_95; }
        auto a_u50 = eval_50_unseen(base_n, ab);
        double au_mean = 0; int au_disq = 0;
        for (const auto& m : a_u50) { if (m.cost >= 900000.0) ++au_disq; else au_mean += m.cost; }
        au_mean = (50 - au_disq) ? au_mean / (50 - au_disq) : 999999.0;
        const auto& rn = base_t.nodes[rem];
        std::cout << "  Drop #" << rem << " (" << FEAT_NAMES[rn.feat] << " " << (rn.is_min ? "min" : "max") << " " << rn.th
                  << "): pub=" << std::setprecision(2) << ac / 5.0 << " (delta=" << std::showpos << (ac / 5.0 - full_pub) << std::noshowpos
                  << ") | unseen50=" << au_mean << " (delta=" << std::showpos << (au_mean - full_u_mean) << std::noshowpos << ", disq=" << au_disq << ")\n";
    }

    std::cout << "\n[4] Threshold Sensitivity on Unseen 50 Seeds Around Optimum:\n";
    for (int nid = 1; nid <= base_t.n_nodes; ++nid) {
        const auto& tn = base_t.nodes[nid];
        if (tn.feat == 0) continue;
        std::vector<double> vals;
        if (tn.feat == 1) vals = {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
        else if (tn.feat == 2) vals = {0, 25, 65, 90, 115};
        else if (tn.feat == 3) vals = {25, 50, 65, 90};
        else vals = {1, 2, 3, 4};
        if (std::find(vals.begin(), vals.end(), tn.th) == vals.end()) { vals.push_back(tn.th); std::sort(vals.begin(), vals.end()); }
        std::cout << "  Node #" << nid << " (" << FEAT_NAMES[tn.feat] << " " << (tn.is_min ? "min" : "max") << ", opt=" << tn.th << "):\n";
        for (double v : vals) {
            Tree cand = base_t; cand.nodes[nid].th = v;
            auto ms = eval_50_unseen(base_n, cand);
            double sum = 0; int dq = 0;
            for (const auto& m : ms) { if (m.cost >= 900000.0) ++dq; else sum += m.cost; }
            double um = (50 - dq) ? sum / (50 - dq) : 999999.0;
            std::cout << "    th=" << std::setw(5) << v << " -> unseen50=" << std::setprecision(2) << um << " (disq=" << dq << ")\n";
        }
    }
}

void run_search(const SimData& sd, const std::filesystem::path& output, int n_threads) {
    auto t0 = std::chrono::steady_clock::now();
    int n_tot = (int)sd.streams.size(); // 55 streams (0=exact seed11, 1..4=mulberry12..15, 5..54=unseen101..150)

    std::vector<Split> all_splits = {
        {0, false, 0.5},
        {2, true, 0}, {2, true, 25}, {2, true, 65}, {2, true, 90}, {2, true, 115},
        {1, false, 0.5}, {1, false, 1.0}, {1, false, 1.5}, {1, false, 2.0}, {1, false, 2.5}, {1, false, 3.5}, {1, false, 4.5}, {1, false, 6.0},
        {1, true, 0.5}, {1, true, 1.0}, {1, true, 1.5}, {1, true, 2.0}, {1, true, 3.0}, {1, true, 4.0}, {1, true, 5.0},
        {4, false, 1}, {4, false, 2}, {4, false, 3}, {4, true, 1}, {4, true, 2},
        {3, true, 25}, {3, true, 50}, {3, true, 65}, {3, false, 25}, {3, false, 65}
    };

    std::vector<Tree> base_pool;
    for (const char* fn : {"cpp_best_18.csv", "cpp_best_19.csv", "cpp_best_20.csv", "answer_exact_sync.csv", "answer_bruteforce.csv"}) {
        std::ifstream chk(fn);
        if (chk.good()) base_pool.push_back(load_csv_tree(sd, fn).second);
    }
    // Also seed with the 7-node robust core (drop #4 & #7 from cpp_best_19) + fresh 3-node roots
    {
        std::ifstream chk("cpp_best_19.csv");
        if (chk.good()) {
            Tree tr = load_csv_tree(sd, "cpp_best_19.csv").second;
            tr.nodes[2].lo = 0;
            base_pool.push_back(canonicalize_tree(tr));
        }
    }
    // Add diverse fresh 3-node skeletons across all root features
    std::vector<Split> r1s = {{2, true, 25}, {2, true, 65}, {1, false, 2.5}, {4, false, 1}, {3, true, 65}, {0, false, 0.5}};
    std::vector<Split> r2s = {{0, false, 0.5}, {4, false, 1}, {3, true, 65}, {1, false, 2.5}, {2, true, 65}};
    for (const auto& r1 : r1s) {
        for (const auto& r2 : r2s) {
            if (r2.feat == r1.feat) continue;
            for (const auto& r3 : r2s) {
                if (r3.feat == r1.feat) continue;
                Tree t3; t3.n_nodes = 3;
                t3.nodes[1] = {r1.feat, r1.is_min, r1.th, 2, 3};
                t3.nodes[2] = {r2.feat, r2.is_min, r2.th, 0, 0};
                t3.nodes[3] = {r3.feat, r3.is_min, r3.th, 0, 0};
                if (is_valid_tree(t3)) base_pool.push_back(t3);
            }
        }
    }

    double global_best_55 = 999999.0;
    int global_best_n = 19;
    Tree global_best_t;

    for (int n_agv : {18, 19, 20}) {
        // Fast 12-stream screen of base_pool -> polish top 6 unique trees on 55 streams
        std::vector<Tree> uniq_base;
        std::set<std::string> bseen;
        for (const auto& bt : base_pool) {
            Tree ct = canonicalize_tree(bt);
            if (is_valid_tree(ct) && bseen.insert(tree_key_str(ct)).second) uniq_base.push_back(ct);
        }
        std::vector<double> bscr(uniq_base.size(), 999999.0);
        {
            std::vector<std::thread> ths;
            for (int ti = 0; ti < n_threads; ++ti) {
                ths.emplace_back([&, ti]() {
                    for (size_t i = ti; i < uniq_base.size(); i += n_threads) {
                        double c0 = run_sim(sd, 0, n_agv, uniq_base[i], 65).cost;
                        if (c0 < 900000.0) {
                            double c11 = eval_streams(sd, n_agv, uniq_base[i], 5, 16, 65);
                            if (c11 < 900000.0) bscr[i] = (c0 + 11.0 * c11) / 12.0;
                        }
                    }
                });
            }
            for (auto& th : ths) th.join();
        }
        std::vector<int> bidx(uniq_base.size());
        std::iota(bidx.begin(), bidx.end(), 0);
        std::sort(bidx.begin(), bidx.end(), [&](int a, int b) { return bscr[a] < bscr[b]; });

        std::vector<std::pair<double, Tree>> elites;
        for (int k = 0; k < std::min((int)bidx.size(), 6); ++k) {
            if (bscr[bidx[k]] >= 900000.0) break;
            double rc = 0;
            Tree rt = fast_robust_polish(sd, n_agv, uniq_base[bidx[k]], rc, n_threads);
            elites.push_back({rc, rt});
        }
        std::sort(elites.begin(), elites.end(), [](auto& a, auto& b) { return a.first < b.first; });
        if (elites.empty()) throw std::runtime_error("No feasible starting tree");
        double best_c = elites[0].first;
        Tree best_t = elites[0].second;
        std::cout << "[Robust55 Init n=" << n_agv << "] start mean55=" << std::fixed << std::setprecision(2) << best_c << "\n" << std::flush;

        std::mt19937 rng(2026 + n_agv);
        for (int round = 1; round <= 4; ++round) {
            std::vector<Tree> cands;
            std::set<std::string> seen;
            seen.insert(tree_key_str(best_t));

            int used_e = 0;
            std::set<std::string> se;
            for (const auto& [ec, et] : elites) {
                if (!se.insert(tree_key_str(et)).second) continue;
                if (++used_e > 3) break;
                for (int nid = 1; nid <= et.n_nodes; ++nid) {
                    for (const auto& sp : all_splits) {
                        Tree c = et;
                        c.nodes[nid].feat = sp.feat; c.nodes[nid].is_min = sp.is_min; c.nodes[nid].th = sp.th;
                        if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                    }
                    Tree sw = et;
                    std::swap(sw.nodes[nid].lo, sw.nodes[nid].hi);
                    sw = canonicalize_tree(sw);
                    if (is_valid_tree(sw) && seen.insert(tree_key_str(sw)).second) cands.push_back(sw);

                    if (et.n_nodes < 9) {
                        for (int br = 0; br < 2; ++br) {
                            int ch = (br == 0) ? et.nodes[nid].lo : et.nodes[nid].hi;
                            if (ch != 0) continue;
                            for (const auto& sp : all_splits) {
                                if (is_noop_attach(et, nid, br == 1, sp.feat, sp.is_min)) continue;
                                Tree c = et;
                                int nid_new = c.n_nodes + 1;
                                c.n_nodes = nid_new;
                                c.nodes[nid_new] = {sp.feat, sp.is_min, sp.th, 0, 0};
                                if (br == 0) c.nodes[nid].lo = nid_new; else c.nodes[nid].hi = nid_new;
                                c = canonicalize_tree(c);
                                if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                            }
                        }
                    }
                    if (et.nodes[nid].lo == 0 && et.nodes[nid].hi == 0 && nid != 1) {
                        Tree c = et;
                        for (int pid = 1; pid <= c.n_nodes; ++pid) {
                            if (c.nodes[pid].lo == nid) c.nodes[pid].lo = 0;
                            if (c.nodes[pid].hi == nid) c.nodes[pid].hi = 0;
                        }
                        c = canonicalize_tree(c);
                        if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
                    }
                }
            }
            for (int trial = 0; trial < 1500; ++trial) {
                Tree c = best_t;
                for (int k = 0; k < 2; ++k) {
                    int nid = 1 + (rng() % c.n_nodes);
                    if ((rng() % 4) == 0) std::swap(c.nodes[nid].lo, c.nodes[nid].hi);
                    else {
                        const auto& sp = all_splits[rng() % all_splits.size()];
                        c.nodes[nid].feat = sp.feat; c.nodes[nid].is_min = sp.is_min; c.nodes[nid].th = sp.th;
                    }
                }
                c = canonicalize_tree(c);
                if (is_valid_tree(c) && seen.insert(tree_key_str(c)).second) cands.push_back(c);
            }

            // Screen on 12 streams -> top 40 on 55 streams -> polish top 4
            std::vector<double> scr(cands.size(), 999999.0);
            {
                std::vector<std::thread> ths;
                for (int ti = 0; ti < n_threads; ++ti) {
                    ths.emplace_back([&, ti]() {
                        for (size_t i = ti; i < cands.size(); i += n_threads) {
                            double c0 = run_sim(sd, 0, n_agv, cands[i], 65).cost;
                            if (c0 < 900000.0) {
                                double c11 = eval_streams(sd, n_agv, cands[i], 5, 16, 65);
                                if (c11 < 900000.0) scr[i] = (c0 + 11.0 * c11) / 12.0;
                            }
                        }
                    });
                }
                for (auto& th : ths) th.join();
            }
            std::vector<int> idx(cands.size());
            std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return scr[a] < scr[b]; });
            int eval_k = std::min((int)idx.size(), 40);
            std::vector<std::pair<double, Tree>> r55(eval_k);
            {
                std::vector<std::thread> ths;
                for (int ti = 0; ti < n_threads; ++ti) {
                    ths.emplace_back([&, ti]() {
                        for (int k = ti; k < eval_k; k += n_threads) {
                            r55[k] = {eval_streams(sd, n_agv, cands[idx[k]], 0, n_tot), cands[idx[k]]};
                        }
                    });
                }
                for (auto& th : ths) th.join();
            }
            std::sort(r55.begin(), r55.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (int k = 0; k < std::min((int)r55.size(), 4); ++k) {
                if (r55[k].first >= 900000.0) continue;
                double pc = 0;
                Tree pt = fast_robust_polish(sd, n_agv, r55[k].second, pc, n_threads);
                elites.push_back({pc, pt});
                if (pc < best_c - 1e-4) { best_c = pc; best_t = pt; }
            }
            std::sort(elites.begin(), elites.end(), [](auto& a, auto& b) { return a.first < b.first; });
            std::cout << "  [Robust55 n=" << n_agv << " Round " << round << "] checked=" << cands.size() << " -> mean55=" << best_c << "\n" << std::flush;
        }

        double s11 = run_sim(sd, 0, n_agv, best_t).cost;
        double pub5 = eval_streams(sd, n_agv, best_t, 0, 5);
        double u50 = eval_streams(sd, n_agv, best_t, 5, n_tot);
        save_csv_tree(sd, (output / ("robust_best_" + std::to_string(n_agv) + ".csv")).string(), n_agv, best_t);
        std::cout << "[Robust Final n=" << n_agv << "] seed11=" << s11 << " | pub5=" << pub5
                  << " | unseen50=" << u50 << " | mean55=" << best_c << " (nodes=" << best_t.n_nodes << ")\n" << std::flush;
        if (best_c < global_best_55) {
            global_best_55 = best_c;
            global_best_n = n_agv;
            global_best_t = best_t;
        }
    }
    save_csv_tree(sd, (output / "answer_final_best.csv").string(), global_best_n, global_best_t);
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "\nSaved overall 55-seed winner to answer_final_best.csv (n=" << global_best_n
              << ", mean55=" << global_best_55 << ") in " << std::fixed << std::setprecision(2)
              << std::chrono::duration<double>(t1 - t0).count() << "s!\n";
}
