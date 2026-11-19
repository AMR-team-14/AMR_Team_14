#include "evaluation.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

bool rejected(const SeedScore& score) { return score.metrics.cost >= 900000.0; }

std::vector<SeedScore> evaluate(SimData& data, const Policy& policy, const std::vector<uint32_t>& seeds) {
    if (seeds.empty()) throw std::runtime_error("Choose at least one seed");
    const auto requested = seeds;
    std::vector<SeedScore> scores;
    for (uint32_t seed : requested) {
        const int index = scenario_index(data, seed);
        scores.push_back({seed, run_sim(data, index, policy.fleet, policy.tree)});
    }
    return scores;
}

std::string evaluation_json(const Policy& policy, const std::vector<SeedScore>& scores) {
    std::ostringstream out;
    out << std::setprecision(15);
    out << "{\"fleet\":" << policy.fleet << ",\"nodes\":[";
    for (int id = 1; id <= policy.tree.n_nodes; ++id) {
        const auto& node = policy.tree.nodes[id];
        if (id > 1) out << ',';
        out << "{\"id\":" << id << ",\"feature\":\"" << FEAT_NAMES[node.feat]
            << "\",\"mode\":\"" << (node.is_min ? "min" : "max")
            << "\",\"threshold\":" << node.th << ",\"low\":" << node.lo
            << ",\"high\":" << node.hi << '}';
    }
    out << "],\"scores\":[";
    double sum = 0.0;
    bool any_rejected = false;
    for (size_t i = 0; i < scores.size(); ++i) {
        if (i) out << ',';
        const auto& score = scores[i];
        const auto& m = score.metrics;
        out << "{\"seed\":" << score.seed << ",\"source\":\""
            << (score.seed == 11 ? "reference" : "generated")
            << "\",\"maxQueue\":" << m.max_q << ",\"endQueue\":" << m.end_q;
        if (rejected(score)) {
            out << ",\"rejected\":true,\"cost\":null}";
            any_rejected = true;
        } else {
            out << ",\"rejected\":false,\"cost\":" << m.cost << ",\"averageWait\":"
                << m.w_bar << ",\"urgent95\":" << m.u_95 << '}';
            sum += m.cost;
        }
    }
    out << "],\"average\":";
    if (any_rejected) out << "null";
    else out << sum / scores.size();
    out << '}';
    return out.str();
}

std::string error_json(const std::string& message) {
    std::ostringstream out;
    out << "{\"error\":\"";
    for (unsigned char c : message) {
        if (c == '\\' || c == '"') out << '\\' << c;
        else if (c < 32) out << ' ';
        else out << c;
    }
    out << "\"}";
    return out.str();
}
