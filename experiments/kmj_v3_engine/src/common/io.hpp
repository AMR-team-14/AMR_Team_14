#pragma once
#include "simulator.hpp"

inline bool prune_node_strict(const Tree& src, int target_nid, Tree& out, int max_n) {
    if (target_nid <= 1 || target_nid > src.n_nodes) return false;
    Tree c = src;
    for (int p = 1; p <= c.n_nodes; ++p) {
        if (c.nodes[p].lo == target_nid) c.nodes[p].lo = 0;
        if (c.nodes[p].hi == target_nid) c.nodes[p].hi = 0;
    }
    out = canonicalize_tree(c);
    return is_strict_tree(out, max_n);
}

inline std::tuple<std::string, int, Tree> load_solution_csv(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open CSV: " << path << "\n"; std::exit(1); }
    std::string line, dir;
    int fleet = 18, max_id = 0;
    Tree t;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> p;
        while (std::getline(ss, tok, ',')) p.push_back(tok);
        if (p.empty()) continue;
        if (p[0] == "dir" && p.size() >= 2) dir = p[1];
        else if (p[0] == "fleet" && p.size() >= 2) fleet = std::stoi(p[1]);
        else if (p[0] == "key" && p.size() >= 7) {
            int nid = std::stoi(p[1]);
            max_id = std::max(max_id, nid);
            int f = -1;
            for (int i = 0; i < 5; ++i) if (p[2] == FEAT_NAMES[i]) f = i;
            t.nodes[nid] = TreeNode{f, p[3] == "min", std::stod(p[4]), std::stoi(p[5]), std::stoi(p[6])};
        }
    }
    t.n_nodes = max_id;
    return {dir, fleet, t};
}

// ============================================================================
// 8. CSV Export & Full Constraint/Brute-Force Verification
// ============================================================================
inline std::string format_th_precise(int feat, double th) {
    if (feat == 0) return "0.5";
    if (std::abs(th - std::round(th)) < 1e-9) return std::to_string((long long)std::llround(th));
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", th);
    return std::string(buf);
}

void save_solution_csv(const std::string& path, const std::string& dir, int fleet, const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ofstream out(path, std::ios::binary);
    out << "dir," << dir << "\r\n";
    out << "fleet," << fleet << "\r\n";
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        out << "key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ","
            << format_th_precise(tn.feat, tn.th) << "," << tn.lo << "," << tn.hi << "\r\n";
    }
}
void print_tree(const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        std::cout << "    key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ","
                  << format_th_precise(tn.feat, tn.th) << "," << tn.lo << "," << tn.hi << "\n";
    }
}
double verify_fast_vs_brute(const CaseData& cd, const CompiledDir& cdir,
                            const std::vector<StreamPack>& streams, int fleet, const Tree& t, int n_check = 10) {
    double max_err = 0.0;
    int N = std::min(n_check, (int)streams.size());
    for (int i = 0; i < N; ++i) {
        auto m1 = run_sim(cd, cdir, streams[i], fleet, t, -1, false);
        auto m2 = run_sim(cd, cdir, streams[i], fleet, t, -1, true);
        if (m1.dq != m2.dq) return 999999.0;
        if (!m1.dq) max_err = std::max(max_err, std::abs(m1.cost - m2.cost));
    }
    return max_err;
}
