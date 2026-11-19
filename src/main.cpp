#include "evaluation.hpp"
#include "search.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

namespace {
void usage() {
    std::cout <<
        "Usage: agv_opt --eval <csv> [--seeds 11,12,13,14,15] [--data-dir docs]\n"
        "       agv_opt --eval-json <csv> [--seeds 151,152,153]\n"
        "       agv_opt --export-arrivals [--seeds 11,12,13,14,15] [--data-dir docs]\n"
        "       agv_opt --analyze <csv> [--data-dir docs]\n"
        "       agv_opt --robust [--output-dir runs/robust] [--data-dir docs]\n";
}

std::vector<uint32_t> parse_seeds(const std::string& values) {
    std::istringstream input(values);
    std::vector<uint32_t> result;
    std::set<uint32_t> seen;
    if (values.empty() || values.back() == ',') throw std::runtime_error("Invalid seed list");
    for (std::string value; std::getline(input, value, ',');) {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid seed: " + value);
        const auto seed = std::stoull(value);
        if (seed > std::numeric_limits<uint32_t>::max() || !seen.insert((uint32_t)seed).second)
            throw std::runtime_error("Out-of-range or duplicate seed: " + value);
        result.push_back((uint32_t)seed);
    }
    return result;
}

void ensure_search_output(const fs::path& output) {
    for (const char* name : {"answer_final_best.csv", "robust_best_18.csv", "robust_best_19.csv", "robust_best_20.csv"}) {
        if (fs::exists(output / name)) throw std::runtime_error("Output exists: " + (output / name).string());
    }
    fs::create_directories(output);
}

void export_arrivals(SimData& model, const std::vector<uint32_t>& seeds) {
    std::cout << "seed,request_index,arrival_min,origin_node,destination_node,urgent\n";
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (uint32_t seed : seeds) {
        const auto& stream = model.streams[scenario_index(model, seed)];
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& request = stream[i];
            std::cout << seed << ',' << i << ',' << request.at << ',' << request.o << ','
                      << request.d << ',' << request.u << '\n';
        }
    }
}

int run(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help") { usage(); return 0; }
    const std::string mode = argv[1];
    const bool eval = mode == "--eval" || mode == "--eval-json";
    const bool robust = mode == "--robust";
    const bool export_only = mode == "--export-arrivals";
    if (!eval && !robust && !export_only && mode != "--analyze") throw std::runtime_error("Unknown mode: " + mode);
    if (!(robust || export_only) && argc < 3) throw std::runtime_error("Provide the policy CSV");
    const std::string csv = (robust || export_only) ? "" : argv[2];
    fs::path directory = "docs", output = "runs/robust";
    std::vector<uint32_t> seeds;
    for (int i = (robust || export_only) ? 2 : 3; i < argc; i += 2) {
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + std::string(argv[i]));
        const std::string option = argv[i];
        if (option == "--data-dir") directory = argv[i + 1];
        else if (option == "--seeds" && (eval || export_only)) seeds = parse_seeds(argv[i + 1]);
        else if (option == "--output-dir" && robust) output = argv[i + 1];
        else throw std::runtime_error("Unknown option: " + option);
    }
    if (robust) ensure_search_output(output);
    SimData model = load_case(read_text((directory / "case.json").string()),
        read_text((directory / "seed11.json").string()), (eval || export_only) ? 0 : 50);
    if (export_only) {
        export_arrivals(model, seeds.empty() ? model.seeds : seeds);
        return 0;
    }
    if (robust) { run_search(model, output, 10); return 0; }
    if (mode == "--analyze") { run_analysis(model, csv, 10); return 0; }

    const auto policy = parse_policy(read_text(csv), model);
    const auto scores = evaluate(model, policy, seeds.empty() ? model.seeds : seeds);
    if (mode == "--eval-json") {
        std::cout << evaluation_json(policy, scores) << '\n';
        return 0;
    }
    double sum = 0.0;
    bool any_rejected = false;
    std::cout << "[" << csv << "] n=" << policy.fleet << ", nodes=" << policy.tree.n_nodes
        << " | " << scores.size() << "-seed:" << std::fixed << std::setprecision(4);
    for (const auto& score : scores) {
        std::cout << " " << score.seed << "=";
        if (rejected(score)) { std::cout << "rejected"; any_rejected = true; }
        else { std::cout << score.metrics.cost; sum += score.metrics.cost; }
    }
    std::cout << " | mean=";
    if (any_rejected) std::cout << "n/a";
    else std::cout << sum / scores.size();
    std::cout << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
