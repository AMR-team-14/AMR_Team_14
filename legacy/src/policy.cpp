#include "policy.hpp"
#include "case_data.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

const char* const FEAT_NAMES[5] = {"urg", "w", "de", "dl", "qo"};

int feat_from_str(const std::string& s) {
    for (int i = 0; i < 5; ++i) {
        if (s == FEAT_NAMES[i]) return i;
    }
    return -1;
}

bool is_valid_tree(const Tree& t) {
    if (t.n_nodes < 1 || t.n_nodes > 9) return false;
    bool visited[10] = {false};
    auto dfs = [&](auto& self, int curr, int path_mask, int feat_mask) -> bool {
        if (curr == 0) return true;
        if (curr < 1 || curr > t.n_nodes) return false;
        if (visited[curr] || (path_mask & (1 << curr))) return false;
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
    for (int i = 1; i <= t.n_nodes; ++i) if (!visited[i]) return false;
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


namespace {
std::string trim(const std::string& input) {
    const auto start = input.find_first_not_of(" \r\t");
    if (start == std::string::npos) return {};
    return input.substr(start, input.find_last_not_of(" \r\t") - start + 1);
}

int integer(const std::string& text, int low, int high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid integer");
    const int value = std::stoi(text);
    if (value < low || value > high) throw std::runtime_error("Integer outside allowed range");
    return value;
}
} // namespace

Policy parse_policy(const std::string& csv, const SimData& data) {
    if (csv.size() > 8192) throw std::runtime_error("Policy CSV exceeds 8 KiB.");
    std::istringstream input(csv.starts_with("\xef\xbb\xbf") ? csv.substr(3) : csv);
    Tree tree;
    int fleet = 0, line_number = 0;
    bool direction = false;
    for (std::string line; std::getline(input, line);) {
        ++line_number;
        if (trim(line).empty()) continue;
        std::vector<std::string> parts;
        for (size_t start = 0, end; ; start = end + 1) {
            end = line.find(',', start);
            parts.push_back(trim(line.substr(start, end - start)));
            if (end == std::string::npos) break;
        }
        try {
            if (parts[0] == "dir" && parts.size() == 2 && !direction) {
                if (parts[1] != data.fixed_dir) throw std::runtime_error("Direction must match the fixed case");
                direction = true;
            } else if (parts[0] == "fleet" && parts.size() == 2 && !fleet) {
                fleet = integer(parts[1], data.min_fleet, data.max_fleet);
            } else if (parts[0] == "key" && parts.size() == 7) {
                const int id = integer(parts[1], 1, data.max_nodes);
                const int feat = feat_from_str(parts[2]);
                const bool is_min = parts[3] == "min";
                size_t consumed = 0;
                const double threshold = std::stod(parts[4], &consumed);
                if (tree.nodes[id].feat != -1 || feat < 0 || (!is_min && parts[3] != "max") ||
                    consumed != parts[4].size() || !std::isfinite(threshold))
                    throw std::runtime_error("Invalid rule");
                tree.nodes[id] = {feat, is_min, threshold, integer(parts[5], 0, data.max_nodes),
                    integer(parts[6], 0, data.max_nodes)};
                tree.n_nodes = std::max(tree.n_nodes, id);
            } else {
                throw std::runtime_error("Invalid or repeated CSV record");
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("CSV line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (!direction || !fleet || !is_valid_tree(tree)) throw std::runtime_error(
        "Missing dir/fleet or invalid tree. Check root #1, child IDs, and repeated features.");
    return {fleet, tree};
}

std::pair<int, Tree> load_csv_tree(const SimData& data, const std::string& path) {
    auto policy = parse_policy(read_text(path), data);
    return {policy.fleet, canonicalize_tree(policy.tree)};
}

void save_csv_tree(const SimData& sd, const std::string& path, int n_agv, const Tree& raw_t) {
    Tree t = canonicalize_tree(raw_t);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path);
    out << "dir," << sd.fixed_dir << "\n";
    out << "fleet," << n_agv << "\n";
    for (int nid = 1; nid <= t.n_nodes; ++nid) {
        const auto& tn = t.nodes[nid];
        out << "key," << nid << "," << FEAT_NAMES[tn.feat] << "," << (tn.is_min ? "min" : "max") << ",";
        if (tn.feat == 0) out << "0.5";
        else if (std::abs(tn.th - std::round(tn.th)) < 1e-9) out << (long long)std::llround(tn.th);
        else out << tn.th;
        out << "," << tn.lo << "," << tn.hi << "\n";
    }
}
