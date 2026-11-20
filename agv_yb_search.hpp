#pragma once

// Included by agv_opt.cpp so the search uses the same evaluator as --verify.
namespace yb {

constexpr int train_first = 4601;
constexpr int pilot_count = 12;
constexpr int train_count = 48;
constexpr int selection_first = 4801;
constexpr int selection_count = 240;
constexpr int audit_first = 6001;
constexpr int audit_count = 3600;
constexpr int beam_width = 12;
constexpr int generation_size = 96;
constexpr double disqualified_cost = 900000.0;

struct Policy {
    int fleet;
    Tree tree;
};

struct Ranked {
    Policy policy;
    double cost;
};

int append_streams(SimData& sd, int first_seed, int count) {
    const int offset = static_cast<int>(sd.streams.size());
    for (int seed = first_seed; seed < first_seed + count; ++seed) {
        const auto& arrivals = sd.streams.emplace_back(generate_arrivals(sd, seed));
        auto& events = sd.base_events.emplace_back();
        events.reserve(arrivals.size());
        for (int i = 0; i < static_cast<int>(arrivals.size()); ++i) {
            events.push_back({round9(arrivals[i].at), 0, i});
        }
    }
    return offset;
}

std::vector<Split> split_library(const Tree& base) {
    std::vector<Split> rules{{0, true, 0.5}, {0, false, 0.5}};
    for (double value : {0.0, 25.0, 65.0, 90.0, 115.0}) rules.push_back({2, true, value});
    for (double value : {25.0, 50.0, 65.0, 90.0}) rules.push_back({3, true, value});
    for (double value : {1.0, 2.0, 3.0}) {
        rules.push_back({4, false, value});
        rules.push_back({4, true, value});
    }
    for (double value : {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 2.45, 2.75, 3.5, 4.5, 5.0}) {
        rules.push_back({1, true, value});
        rules.push_back({1, false, value});
    }
    // Give splits with demonstrated performance more sampling weight.
    for (int i = 1; i <= base.n_nodes; ++i) {
        const auto& node = base.nodes[i];
        for (int copy = 0; copy < 2; ++copy) rules.push_back({node.feat, node.is_min, node.th});
    }
    return rules;
}

int pick(std::mt19937& rng, int count) {
    return std::uniform_int_distribution<int>(0, count - 1)(rng);
}

double move_wait(double threshold, std::mt19937& rng) {
    constexpr int steps[] = {-10, -6, -4, -2, -1, 1, 2, 4, 6, 10};
    const long ticks = std::lround(threshold * 20.0);
    return std::max(1L, ticks + steps[pick(rng, 10)]) / 20.0;
}

Tree replace_rules(Tree tree, const std::vector<Split>& rules, std::mt19937& rng) {
    std::vector<int> nodes(tree.n_nodes);
    std::iota(nodes.begin(), nodes.end(), 1);
    std::shuffle(nodes.begin(), nodes.end(), rng);
    const int change_count = std::min(tree.n_nodes, 3 + pick(rng, 2));
    for (int i = 0; i < change_count; ++i) {
        auto& node = tree.nodes[nodes[i]];
        if (node.feat == 1 && pick(rng, 4) != 0) {
            node.th = move_wait(node.th, rng);
            continue;
        }
        std::vector<Split> alternatives;
        const bool keep_feature = pick(rng, 4) != 0;
        for (const auto& rule : rules) {
            if (keep_feature && rule.feat != node.feat) continue;
            if (rule.feat == node.feat && rule.is_min == node.is_min && rule.th == node.th) continue;
            alternatives.push_back(rule);
        }
        if (alternatives.empty()) continue;
        const auto& rule = alternatives[pick(rng, static_cast<int>(alternatives.size()))];
        node.feat = rule.feat;
        node.is_min = rule.is_min;
        node.th = rule.th;
    }
    return tree;
}

struct Cut {
    int parent;
    bool high;
    int removed;
    int ancestor_features;
};

int list_cuts(const Ast& tree, int node, int parent, bool high, int used, std::vector<Cut>& cuts) {
    if (node < 0) return 0;
    const auto& n = tree[node];
    const int mask = used | (1 << n.feat);
    const int size = 1 + list_cuts(tree, n.lo, node, false, mask, cuts)
                       + list_cuts(tree, n.hi, node, true, mask, cuts);
    if (size >= 2) cuts.push_back({parent, high, size, used});
    return size;
}

int grow(Ast& ast, int size, int used, const std::vector<Split>& rules, std::mt19937& rng) {
    if (size == 0) return -1;
    std::vector<Split> available;
    for (const auto& rule : rules) {
        if (!(used & (1 << rule.feat))) available.push_back(rule);
    }
    const Split rule = available[pick(rng, static_cast<int>(available.size()))];
    const int mask = used | (1 << rule.feat);
    const int capacity = (1 << (5 - std::popcount(static_cast<unsigned>(mask)))) - 1;
    const int lo_min = std::max(0, size - 1 - capacity);
    const int lo_max = std::min(capacity, size - 1);
    const int lo_size = lo_min + pick(rng, lo_max - lo_min + 1);
    const int left = grow(ast, lo_size, mask, rules, rng);
    const int right = grow(ast, size - 1 - lo_size, mask, rules, rng);
    ast.push_back({rule.feat, rule.is_min, rule.th, left, right});
    return static_cast<int>(ast.size()) - 1;
}

Tree rebuild_subtree(const Tree& tree, const std::vector<Split>& rules, std::mt19937& rng) {
    auto [ast, root] = tree_to_ast(tree);
    std::vector<Cut> cuts;
    list_cuts(ast, root, -1, false, 0, cuts);
    if (cuts.empty()) return replace_rules(tree, rules, rng);
    // Try both large neighborhoods and targeted repairs in promising subtrees.
    const Cut cut = cuts[pick(rng, static_cast<int>(cuts.size()))];
    const int capacity = (1 << (5 - std::popcount(static_cast<unsigned>(cut.ancestor_features)))) - 1;
    const int max_size = std::min(capacity, 9 - tree.n_nodes + cut.removed);
    const int size = std::clamp(cut.removed + pick(rng, 4) - 2, 1, max_size);
    const int replacement = grow(ast, size, cut.ancestor_features, rules, rng);
    if (cut.parent < 0) root = replacement;
    else if (cut.high) ast[cut.parent].hi = replacement;
    else ast[cut.parent].lo = replacement;
    return ast_to_valid_tree(ast, root);
}

std::vector<Ranked> rank(const SimData& sd, const std::vector<Policy>& policies,
                         int start, int count, int threads) {
    std::vector<Ranked> ranked(policies.size());
    parallel_for(static_cast<int>(policies.size()), threads, [&](int i) {
        const auto& p = policies[i];
        ranked[i] = {p, eval_streams(sd, p.fleet, p.tree, start, start + count)};
    });
    std::erase_if(ranked, [](const Ranked& p) { return p.cost >= disqualified_cost; });
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) { return a.cost < b.cost; });
    return ranked;
}

std::vector<Policy> top(const std::vector<Ranked>& ranked, int count) {
    std::vector<Policy> policies;
    for (int i = 0; i < std::min(count, static_cast<int>(ranked.size())); ++i) {
        policies.push_back(ranked[i].policy);
    }
    return policies;
}

int select_challenger(SimData& sd, const Policy& baseline, const std::vector<Policy>& finalists,
                      const std::string& out, int total, bool new_tree_only, int threads) {
    const int selection = append_streams(sd, selection_first, selection_count);
    const auto ranked = rank(sd, finalists, selection, selection_count, threads);
    const std::string baseline_key = tree_key_str(baseline.tree);
    const auto challenger = std::find_if(ranked.begin(), ranked.end(), [&](const Ranked& r) {
        const bool new_tree = tree_key_str(r.policy.tree) != baseline_key;
        return new_tree || (!new_tree_only && r.policy.fleet != baseline.fleet);
    });
    if (challenger == ranked.end()) {
        std::cerr << "No eligible challenger in the selection seeds.\n";
        return 1;
    }
    const auto& chosen = challenger->policy;
    save_csv_tree(sd, out, chosen.fleet, chosen.tree);
    if (!std::filesystem::is_regular_file(out) || std::filesystem::file_size(out) == 0) {
        std::cerr << "Could not write challenger CSV: " << out << '\n';
        return 1;
    }
    const double original_cost =
        eval_streams(sd, baseline.fleet, baseline.tree, selection, selection + selection_count);
    std::cout << std::fixed << std::setprecision(5)
              << "[locked] unique=" << total << " finalists=" << finalists.size()
              << " fleet=" << chosen.fleet << " nodes=" << chosen.tree.n_nodes
              << " selection240=" << challenger->cost << " baseline=" << original_cost
              << " delta=" << challenger->cost - original_cost << " csv=" << out << '\n';
    return 0;
}

int search(SimData& sd, const Policy& baseline, const std::string& out, int rounds, int threads) {
    const auto rules = split_library(baseline.tree);
    std::mt19937 rng(20261120);
    const int train = append_streams(sd, train_first, train_count);
    std::vector<Policy> finalists;
    int total = 0;
    for (int fleet = std::max(1, baseline.fleet - 1); fleet <= std::min(30, baseline.fleet + 1); ++fleet) {
        const Policy original{fleet, baseline.tree};
        std::vector<Ranked> elite = rank(sd, {original}, train, train_count, threads);
        const std::string original_key = tree_key_str(baseline.tree);
        std::set<std::string> seen{original_key};
        int screened = 0, full = 0;
        for (int round = 0; round < rounds; ++round) {
            std::vector<Policy> proposals;
            for (int attempt = 0; attempt < generation_size * 12
                 && static_cast<int>(proposals.size()) < generation_size; ++attempt) {
                const Tree& parent = (elite.empty() || pick(rng, 4) == 0)
                    ? original.tree : elite[pick(rng, static_cast<int>(elite.size()))].policy.tree;
                Tree next = pick(rng, 2) == 0
                    ? replace_rules(parent, rules, rng) : rebuild_subtree(parent, rules, rng);
                if (is_valid_tree(next) && seen.insert(tree_key_str(next)).second) {
                    proposals.push_back({fleet, next});
                }
            }
            const auto pilot = rank(sd, proposals, train, pilot_count, threads);
            const auto promoted = top(pilot, generation_size / 4);
            const auto evaluated = rank(sd, promoted, train, train_count, threads);
            screened += static_cast<int>(proposals.size());
            full += static_cast<int>(promoted.size());
            elite.insert(elite.end(), evaluated.begin(), evaluated.end());
            std::stable_sort(elite.begin(), elite.end(),
                             [](const Ranked& a, const Ranked& b) { return a.cost < b.cost; });
            if (static_cast<int>(elite.size()) > beam_width) elite.resize(beam_width);
            if (!elite.empty()) {
                std::cout << "[search] fleet=" << fleet << " round=" << round + 1 << '/' << rounds
                          << " train48=" << std::fixed << std::setprecision(3) << elite.front().cost
                          << std::endl;
            }
        }
        auto kept = top(elite, beam_width);
        if (std::none_of(kept.begin(), kept.end(), [&](const Policy& p) {
            return tree_key_str(p.tree) == original_key;
        })) kept.push_back(original);
        finalists.insert(finalists.end(), kept.begin(), kept.end());
        total += screened;
        std::cout << "[shortlist] fleet=" << fleet << " unique=" << screened
                  << " train48_count=" << full << " finalists=" << kept.size() << std::endl;
    }
    return select_challenger(sd, baseline, finalists, out, total, false, threads);
}

int deadline_search(SimData& sd, const Policy& baseline, const std::string& out, int threads) {
    std::vector<int> deadline_nodes;
    std::vector<Tree> shapes{baseline.tree};
    for (int i = 1; i <= baseline.tree.n_nodes; ++i) {
        const auto& node = baseline.tree.nodes[i];
        if (node.feat == 4 && !node.is_min && node.th == 1.0) {
            Tree queued = baseline.tree;
            queued.nodes[i].th = 2.0;
            shapes.push_back(queued);
        }
        if (node.feat != 1 || !node.is_min) continue;
        deadline_nodes.push_back(i);
        if (!node.lo || node.hi) continue;
        const auto& child = baseline.tree.nodes[node.lo];
        if (child.feat != 0 || child.is_min || child.lo || child.hi) continue;
        // Compare fresh-first against urgent-first for the same two tests.
        Tree urgent_first = baseline.tree;
        urgent_first.nodes[i] = {0, false, 0.5, 0, node.lo};
        urgent_first.nodes[node.lo] = {1, true, node.th, 0, 0};
        shapes.push_back(urgent_first);
    }
    if (deadline_nodes.size() != 2) {
        throw std::invalid_argument("deadline search expects exactly two w,min nodes");
    }

    const int train = append_streams(sd, train_first, train_count);
    const std::string original_key = tree_key_str(baseline.tree);
    constexpr int offsets[] = {-8, -4, -2, -1, 0, 1, 2, 4, 8};
    std::vector<Policy> finalists;
    int total = 0;
    for (int fleet = std::max(1, baseline.fleet - 1); fleet <= baseline.fleet; ++fleet) {
        std::vector<Policy> proposals;
        std::set<std::string> seen{original_key};
        for (const Tree& shape : shapes) {
            std::vector<int> wait;
            for (int i = 1; i <= shape.n_nodes; ++i) {
                if (shape.nodes[i].feat == 1 && shape.nodes[i].is_min) wait.push_back(i);
            }
            for (int a : offsets) {
                for (int b : offsets) {
                    Tree next = shape;
                    next.nodes[wait[0]].th = std::max(1L, std::lround(shape.nodes[wait[0]].th * 20.0) + a) / 20.0;
                    next.nodes[wait[1]].th = std::max(1L, std::lround(shape.nodes[wait[1]].th * 20.0) + b) / 20.0;
                    if (is_valid_tree(next) && seen.insert(tree_key_str(next)).second) {
                        proposals.push_back({fleet, next});
                    }
                }
            }
        }
        const auto pilot = rank(sd, proposals, train, pilot_count, threads);
        const auto evaluated = rank(sd, top(pilot, 48), train, train_count, threads);
        const auto kept = top(evaluated, beam_width);
        finalists.insert(finalists.end(), kept.begin(), kept.end());
        total += static_cast<int>(proposals.size());
        std::cout << "[deadlines] fleet=" << fleet << " unique=" << proposals.size()
                  << " finalists=" << kept.size() << std::endl;
    }
    return select_challenger(sd, baseline, finalists, out, total, true, threads);
}

int audit(SimData& sd, const Policy& reference, const Policy& challenger, int first_seed, int threads) {
    const int saved = static_cast<int>(sd.streams.size());
    int dq_ref = 0, dq_candidate = 0;
    int pairs = 0;
    double sum_ref = 0.0, sum_candidate = 0.0, delta_mean = 0.0, m2 = 0.0;
    double wait_delta = 0.0, urgent_delta = 0.0;
    int max_q = 0, end_q = 0;
    constexpr int batch_size = 120;
    for (int done = 0; done < audit_count; done += batch_size) {
        const int count = std::min(batch_size, audit_count - done);
        const int start = append_streams(sd, first_seed + done, count);
        const auto base = eval_stream_range(sd, reference.fleet, reference.tree, start, count, threads);
        const auto test = eval_stream_range(sd, challenger.fleet, challenger.tree, start, count, threads);
        for (int i = 0; i < count; ++i) {
            const bool valid_ref = base[i].cost < disqualified_cost;
            const bool valid_test = test[i].cost < disqualified_cost;
            dq_ref += !valid_ref;
            dq_candidate += !valid_test;
            max_q = std::max(max_q, test[i].max_q);
            end_q = std::max(end_q, test[i].end_q);
            if (!valid_ref || !valid_test) continue;
            const double d = test[i].cost - base[i].cost;
            sum_ref += base[i].cost;
            sum_candidate += test[i].cost;
            wait_delta += test[i].w_bar - base[i].w_bar;
            urgent_delta += test[i].u_95 - base[i].u_95;
            ++pairs;
            const double residual = d - delta_mean;
            delta_mean += residual / pairs;
            m2 += residual * (d - delta_mean);
        }
        sd.streams.resize(saved);
        sd.base_events.resize(saved);
    }

    std::cout << "[audit] seeds=" << first_seed << ".." << first_seed + audit_count - 1
              << " paired=" << pairs << " dq_reference=" << dq_ref
              << " dq_challenger=" << dq_candidate << " maxQ_challenger=" << max_q
              << " endQ_challenger=" << end_q << '\n';
    if (pairs != audit_count) {
        std::cout << "RESULT=KEEP_BASELINE (at least one disqualification)\n";
        return 0;
    }
    const double se = std::sqrt(m2 / (pairs - 1) / pairs);
    const double upper = delta_mean + 1.646 * se; // One-sided 95%, 3599 degrees of freedom.
    const bool promote = delta_mean + 2.0 * se < 0.0;
    std::cout << std::fixed << std::setprecision(5)
              << "[paired] reference=" << sum_ref / pairs << " challenger=" << sum_candidate / pairs
              << " delta=" << delta_mean << " se=" << se << " upper95=" << upper
              << " delta_wait_min=" << wait_delta / pairs << " delta_urgent95_min=" << urgent_delta / pairs
              << "\nRESULT=" << (promote ? "SIGNIFICANT_IMPROVEMENT" : "KEEP_BASELINE") << '\n';
    return 0;
}

Policy read_policy(const std::string& path, const std::string& direction) {
    const auto [fleet, tree] = load_csv_tree(path, &direction);
    return {fleet, tree};
}

int run(SimData& sd, int argc, char** argv, int threads) {
    const std::string mode = argv[1];
    if (argc != 4 && !(mode != "--yb-deadlines" && argc == 5)) {
        std::cerr << "Usage: --yb-search <baseline.csv> <new.csv> [rounds=8]"
                  << " | --yb-deadlines <baseline.csv> <new.csv>"
                  << " | --yb-audit <baseline.csv> <challenger.csv> [first_seed=6001]\n";
        return 2;
    }
    try {
        const Policy baseline = read_policy(argv[2], sd.fixed_dir);
        int option = mode == "--yb-audit" ? audit_first : 8;
        if (argc == 5) {
            const std::string number = argv[4];
            std::size_t parsed = 0;
            option = std::stoi(number, &parsed);
            if (parsed != number.size()) throw std::invalid_argument("expected an integer");
        }
        if (mode == "--yb-audit") {
            if (option < audit_first || option > 2000000000) {
                throw std::invalid_argument("first_seed must be between 6001 and 2000000000");
            }
            return audit(sd, baseline, read_policy(argv[3], sd.fixed_dir), option, threads);
        }
        if (std::filesystem::exists(argv[3])) {
            throw std::invalid_argument("choose a new output path; existing files are preserved");
        }
        if (mode == "--yb-deadlines") return deadline_search(sd, baseline, argv[3], threads);
        if (option < 1 || option > 64) {
            throw std::invalid_argument("rounds must be an integer between 1 and 64");
        }
        return search(sd, baseline, argv[3], option, threads);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

} // namespace yb
