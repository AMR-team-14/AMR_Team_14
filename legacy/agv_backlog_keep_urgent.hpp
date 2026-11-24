#pragma once

// Isolate the ordinary-request rule before paying for a ninth submission slot.
namespace backlog_keep_urgent {

constexpr int train_first = 37001, train_count = 320;
constexpr int select_first = 38001, select_count = 600;
constexpr int audit_first = 39001, audit_count = 3600;
constexpr int example_ticks = 80;
constexpr double service_minutes = 8.0;

struct Family {
    yb::Policy reference;
    int near_urgency;
    int far_wait;
    int far_urgency;

    Tree without_far_leaf() const {
        Tree tree = reference.tree;
        tree.nodes[far_wait].lo = 0;
        return canonicalize_tree(tree);
    }

    Tree legal(int ticks) const {
        Tree tree = reference.tree;
        tree.nodes[far_wait].lo = 0;
        tree.nodes[near_urgency].lo = far_urgency;
        tree.nodes[far_urgency] = {1, false, ticks / 20.0, 0, 0};
        return tree;
    }
};

Family make_family(const yb::Policy& policy) {
    const auto original = aged_backlog::make_family(policy);
    const auto& tree = policy.tree;
    const auto& far_distance = tree.nodes[tree.nodes[1].hi];
    const auto& far_queue = tree.nodes[far_distance.lo];
    const int wait_id = far_queue.hi;
    const auto& wait = tree.nodes[wait_id];
    const auto& leaf = tree.nodes[wait.lo];
    if (!tree.nodes[1].hi || far_distance.feat != 3 || !far_distance.is_min ||
        !far_distance.lo || far_queue.feat != 4 || far_queue.is_min ||
        !wait_id || wait.feat != 1 || !wait.is_min || wait.th != 0.75 ||
        !wait.lo || leaf.feat != 0 || leaf.is_min || leaf.lo || leaf.hi) {
        throw std::invalid_argument("expected the baseline far fresh-urgent leaf");
    }
    Family family{policy, original.urgency_node, wait_id, wait.lo};
    if (!is_valid_tree(family.without_far_leaf()) || !is_valid_tree(family.legal(example_ticks))) {
        throw std::invalid_argument("invalid legal comparison tree");
    }
    return family;
}

struct Variant {
    Tree tree;
    int ticks;
    int virtual_parent = 0;
};

Variant virtual_rule(const Family& family, int ticks) {
    return {family.reference.tree, ticks, family.near_urgency};
}

SimMetrics evaluate(const SimData& sd, int stream, int fleet, const Variant& variant,
                    bool brute = false, SimTrace* trace = nullptr) {
    const ExperimentalWaitPriority extra{variant.virtual_parent, variant.ticks / 20.0};
    return run_sim(sd, stream, fleet, variant.tree, -1, brute, trace,
                   variant.virtual_parent ? &extra : nullptr);
}

void verify_virtual(const SimData& sd, const Family& family, int ticks) {
    // Match the same virtual leaf to an actual 9-node leaf on the 8-node reference.
    const Variant legal{family.legal(ticks), ticks};
    const int control_near = family.without_far_leaf().nodes[1].lo;
    const int control_urgency = family.without_far_leaf().nodes[control_near].hi;
    const Variant virtual_on_control{family.without_far_leaf(), ticks, control_urgency};
    const Variant full = virtual_rule(family, ticks);
    for (int s = 0; s < 15; ++s) {
        const auto a = evaluate(sd, s, family.reference.fleet, legal);
        const auto b = evaluate(sd, s, family.reference.fleet, virtual_on_control, true);
        const auto c = evaluate(sd, s, family.reference.fleet, full);
        const auto d = evaluate(sd, s, family.reference.fleet, full, true);
        if (std::abs(a.cost - b.cost) > 1e-9 || std::abs(c.cost - d.cost) > 1e-9) {
            throw std::runtime_error("virtual wait leaf failed dispatch parity");
        }
    }
}

struct Score {
    int ticks;
    aged_backlog::Paired original;
    aged_backlog::Paired matched;
};

bool active(const Score& s) {
    return s.original.rejected == 0 && s.matched.rejected == 0 && s.matched.changed_cost > 0;
}

std::vector<Score> rank(const SimData& sd, const Family& family, const std::set<int>& thresholds,
                        bool diagnostic, int start, int count, int threads) {
    const auto& reference = family.reference;
    const auto original =
        eval_stream_range(sd, reference.fleet, reference.tree, start, count, threads);
    const auto matched = diagnostic ? original :
        eval_stream_range(sd, reference.fleet, family.without_far_leaf(), start, count, threads);
    const std::vector<int> grid(thresholds.begin(), thresholds.end());
    std::vector<Score> scores(grid.size());
    parallel_for(static_cast<int>(grid.size()), threads, [&](int k) {
        auto& score = scores[k];
        const int t = grid[k];
        score.ticks = t;
        const auto variant = diagnostic ? virtual_rule(family, t) : Variant{family.legal(t), t};
        for (int i = 0; i < count; ++i) {
            const auto test = evaluate(sd, start + i, reference.fleet, variant);
            score.original.add(original[i], test);
            score.matched.add(matched[i], test);
        }
    });
    std::sort(scores.begin(), scores.end(), [](const Score& a, const Score& b) {
        return std::tie(a.original.rejected, a.original.mean, a.ticks)
             < std::tie(b.original.rejected, b.original.mean, b.ticks);
    });
    return scores;
}

void write_rank(std::ofstream& file, const char* variant, const char* stage,
                const std::vector<Score>& scores) {
    for (const auto& s : scores) {
        file << variant << ',' << stage << ',' << s.ticks / 20.0 << ','
             << s.original.n << ',' << s.original.rejected << ','
             << s.original.mean << ',' << s.original.se() << ','
             << s.matched.mean << ',' << s.matched.se() << ',' << s.matched.changed_cost << '\n';
    }
}

int tune(const SimData& sd, const Family& family, bool diagnostic, int train, int select,
         int threads, std::ofstream& file) {
    const char* name = diagnostic ? "virtual" : "legal";
    const std::set<int> grid{10,20,30,40,50,60,80,100,120,160,200,240,320,398,480};
    const auto coarse = rank(sd, family, grid, diagnostic, train, train_count, threads);
    write_rank(file, name, "coarse", coarse);
    std::set<int> fine;
    int centers = 0;
    for (const auto& s : coarse) {
        if (!active(s)) continue;
        if (++centers > 3) break;
        for (int t = std::max(1, s.ticks - 5); t <= s.ticks + 5; ++t) {
            if (!grid.contains(t)) fine.insert(t);
        }
    }
    const auto refined = rank(sd, family, fine, diagnostic, train, train_count, threads);
    write_rank(file, name, "refine", refined);
    std::set<int> shortlist{example_ticks};
    for (const auto* pool : {&coarse, &refined}) {
        int kept = 0;
        for (const auto& s : *pool) {
            if (!active(s)) continue;
            if (++kept > 6) break;
            shortlist.insert(s.ticks);
        }
    }
    const auto selected = rank(sd, family, shortlist, diagnostic, select, select_count, threads);
    write_rank(file, name, "selection", selected);
    const auto best = std::find_if(selected.begin(), selected.end(), active);
    if (best == selected.end()) throw std::runtime_error(std::string("no active threshold for ") + name);
    std::cout << "[keep urgent] " << name << " locked T=" << best->ticks / 20.0
              << " delta_baseline=" << best->original.mean << std::endl;
    return best->ticks;
}

struct Outcome {
    SimMetrics scores;
    int general_count = 0;
    int general_late = 0;
};

Outcome measure(const SimData& sd, int stream, int fleet, const Variant& variant) {
    SimTrace trace;
    Outcome out{evaluate(sd, stream, fleet, variant, false, &trace)};
    if (!aged_backlog::valid(out.scores)) return out;
    const auto& arrivals = sd.streams[stream];
    for (std::size_t i = 0; i < arrivals.size(); ++i) {
        const auto& request = arrivals[i];
        if (request.u || request.at < sd.warm || request.at > sd.t_end - service_minutes) continue;
        ++out.general_count;
        if (trace.pickup[i] < 0 || trace.pickup[i] - request.at > service_minutes) ++out.general_late;
    }
    return out;
}

struct Arm {
    const char* name;
    Variant variant;
    std::vector<Outcome> data;
};

void audit(SimData& sd, const Family& family, int diagnostic_t, int legal_t,
           const std::filesystem::path& out, int threads) {
    std::array<Arm, 6> arms{{
        {"01_baseline", {family.reference.tree, 0}, {}},
        {"02_without_far_leaf", {family.without_far_leaf(), 0}, {}},
        {"03_virtual_best", virtual_rule(family, diagnostic_t), {}},
        {"04_legal_best", {family.legal(legal_t), legal_t}, {}},
        {"05_virtual_T4", virtual_rule(family, example_ticks), {}},
        {"06_legal_T4", {family.legal(example_ticks), example_ticks}, {}}
    }};

    constexpr int batch = 120;
    for (int done = 0; done < audit_count; done += batch) {
        sd.streams.clear();
        sd.base_events.clear();
        const int count = std::min(batch, audit_count - done);
        const int start = yb::append_streams(sd, audit_first + done, count);
        for (auto& arm : arms) {
            arm.data.resize(done + count);
            parallel_for(count, threads, [&](int i) {
                arm.data[done + i] = measure(sd, start + i, family.reference.fleet, arm.variant);
            });
        }
    }

    std::ofstream table(out / "results" / "summary.csv");
    std::ofstream pairs(out / "results" / "comparisons.csv");
    std::ofstream raw(out / "results" / "validation_by_seed.csv");
    table << "policy,T_min,seeds,dq,mean_cost,urgent_p95,late_general_per_1000,worst_5pct_mean\n"
          << std::fixed << std::setprecision(6);
    pairs << "comparison,paired,rejected,delta_cost,se_cost,delta_urgent_p95,delta_late_general_per_1000,se_late_general,changed_cost\n"
          << std::fixed << std::setprecision(6);
    raw << "seed,policy,cost,mean_wait,urgent_p95,general_count,general_late,dq\n"
        << std::fixed << std::setprecision(8);
    for (const auto& arm : arms) {
        std::vector<double> cost;
        double urgent = 0.0, late = 0.0;
        for (int i = 0; i < audit_count; ++i) {
            const auto& x = arm.data[i];
            raw << audit_first + i << ',' << arm.name << ',' << x.scores.cost << ','
                << x.scores.w_bar << ',' << x.scores.u_95 << ',' << x.general_count << ','
                << x.general_late << ',' << !aged_backlog::valid(x.scores) << '\n';
            if (!aged_backlog::valid(x.scores)) continue;
            cost.push_back(x.scores.cost);
            urgent += x.scores.u_95;
            late += 1000.0 * x.general_late / x.general_count;
        }
        std::sort(cost.begin(), cost.end());
        const int n = static_cast<int>(cost.size()), tail = (n + 19) / 20;
        table << arm.name << ',' << arm.variant.ticks / 20.0 << ',' << audit_count << ','
              << audit_count - n << ',';
        if (!n) table << "NA,NA,NA,NA\n";
        else table << std::accumulate(cost.begin(), cost.end(), 0.0) / n << ',' << urgent / n
                   << ',' << late / n << ','
                   << std::accumulate(cost.end() - tail, cost.end(), 0.0) / tail << '\n';
    }

    for (const auto [test, ref] : std::array<std::pair<int,int>, 8>{
             {{1,0},{2,0},{3,1},{3,0},{4,0},{5,1},{5,0},{5,4}}}) {
        aged_backlog::Paired cost;
        double late = 0.0, late_m2 = 0.0;
        for (int i = 0; i < audit_count; ++i) {
            const auto& a = arms[ref].data[i];
            const auto& b = arms[test].data[i];
            cost.add(a.scores, b.scores);
            if (!aged_backlog::valid(a.scores) || !aged_backlog::valid(b.scores)) continue;
            const double d = 1000.0 * (double(b.general_late) / b.general_count -
                                       double(a.general_late) / a.general_count);
            const double residual = d - late;
            late += residual / cost.n;
            late_m2 += residual * (d - late);
        }
        const double late_se = cost.n > 1 ? std::sqrt(late_m2 / (cost.n - 1) / cost.n) : 0;
        pairs << arms[test].name << "_minus_" << arms[ref].name << ',' << cost.n << ','
              << cost.rejected << ',' << cost.mean << ',' << cost.se() << ','
              << cost.urgent / std::max(1, cost.n) << ',' << late << ',' << late_se << ','
              << cost.changed_cost << '\n';
    }
    table.close();
    pairs.close();
    raw.close();
    if (!table || !pairs || !raw) throw std::runtime_error("failed to write keep-urgent audit");
}

int run(SimData& sd, int argc, char** argv, int threads) {
    if (argc != 4) {
        std::cerr << "Usage: --yb-backlog-keep-urgent <baseline.csv> <new-results-directory>\n";
        return 2;
    }
    try {
        const Family family = make_family(yb::read_policy(argv[2], sd.fixed_dir));
        const std::filesystem::path out = argv[3];
        if (!std::filesystem::create_directory(out)) throw std::invalid_argument("choose a new results directory");
        std::filesystem::create_directory(out / "trees");
        std::filesystem::create_directory(out / "results");
        const int train = yb::append_streams(sd, train_first, train_count);
        const int select = yb::append_streams(sd, select_first, select_count);
        std::ofstream thresholds(out / "results" / "thresholds.csv");
        thresholds << "family,stage,T_min,paired,dq,delta_vs_baseline,se_vs_baseline,delta_vs_matched,se_vs_matched,changed_vs_matched\n"
                   << std::fixed << std::setprecision(6);
        const int diagnostic_t = tune(sd, family, true, train, select, threads, thresholds);
        const int legal_t = tune(sd, family, false, train, select, threads, thresholds);
        thresholds.close();
        if (!thresholds) throw std::runtime_error("failed to write thresholds");
        for (int t : {example_ticks, diagnostic_t, legal_t}) verify_virtual(sd, family, t);
        const std::array trees{family.reference.tree, family.without_far_leaf(),
                               family.legal(legal_t), family.legal(example_ticks)};
        constexpr std::array names{"01_baseline.csv", "02_without_far_leaf.csv", "04_legal_best.csv", "06_legal_T4.csv"};
        for (std::size_t i = 0; i < trees.size(); ++i) {
            const auto p = out / "trees" / names[i];
            save_csv_tree(sd, p.string(), family.reference.fleet, trees[i]);
            if (!std::filesystem::is_regular_file(p) || !std::filesystem::file_size(p)) {
                throw std::runtime_error("failed to write legal tree");
            }
        }
        audit(sd, family, diagnostic_t, legal_t, out, threads);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

} // namespace backlog_keep_urgent
