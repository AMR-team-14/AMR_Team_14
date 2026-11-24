#pragma once

// Fixed-shape comparison for the near, congested, nonurgent branch.
namespace aged_backlog {

constexpr int discovery_first = 31001;
constexpr int discovery_count = 240;
constexpr int selection_first = 32001;
constexpr int selection_count = 500;
constexpr int audit_first = 33001;
constexpr int audit_count = 3600;
constexpr int ticks_per_minute = 20;

struct PolicyFamily {
    yb::Policy original;
    Tree control;
    int urgency_node;
    int donor_node;

    Tree with_protection(int ticks) const {
        Tree tree = original.tree;
        tree.nodes[urgency_node].hi = 0;
        tree.nodes[urgency_node].lo = donor_node;
        tree.nodes[donor_node] = {1, false, double(ticks) / ticks_per_minute, 0, 0};
        return tree;
    }
};

PolicyFamily make_family(const yb::Policy& baseline) {
    const auto& tree = baseline.tree;
    const auto& root = tree.nodes[1];
    const int near = root.lo;
    const auto& queue = tree.nodes[near];
    const int urgent = queue.hi;
    const auto& urgency = tree.nodes[urgent];
    const int donor = urgency.hi;
    const auto& wait = tree.nodes[donor];
    if (baseline.fleet != 19 || tree.n_nodes != 9 || root.feat != 2 || !root.is_min ||
        root.th != 65.0 || !near || queue.feat != 4 || queue.is_min || queue.th != 1.0 ||
        queue.lo || !urgent || urgency.feat != 0 || urgency.is_min || urgency.lo ||
        !donor || wait.feat != 1 || !wait.is_min || wait.th != 2.45 || wait.lo || wait.hi) {
        throw std::invalid_argument("expected the 19-AGV baseline with the near urgent w,min,2.45 leaf");
    }
    Tree tree_without_rule = tree;
    tree_without_rule.nodes[urgent].hi = 0;
    PolicyFamily family{baseline, canonicalize_tree(tree_without_rule), urgent, donor};
    if (!is_valid_tree(family.control) || !is_valid_tree(family.with_protection(80))) {
        throw std::invalid_argument("backlog variants violate decision-tree constraints");
    }
    return family;
}

bool valid(const SimMetrics& score) {
    return score.cost < yb::disqualified_cost;
}

struct Paired {
    int n = 0;
    int rejected = 0;
    int changed_cost = 0;
    double mean = 0.0;
    double m2 = 0.0;
    double wait = 0.0;
    double urgent = 0.0;

    void add(const SimMetrics& base, const SimMetrics& test) {
        if (!valid(base) || !valid(test)) {
            ++rejected;
            return;
        }
        const double d = test.cost - base.cost;
        changed_cost += std::abs(d) > 1e-9;
        const double residual = d - mean;
        mean += residual / ++n;
        m2 += residual * (d - mean);
        wait += test.w_bar - base.w_bar;
        urgent += test.u_95 - base.u_95;
    }

    double se() const {
        return n > 1 ? std::sqrt(m2 / (n - 1) / n) : 0.0;
    }

    bool improves() const {
        return n > 1 && rejected == 0 && mean + 2.0 * se() < 0.0;
    }
};

struct ThresholdScore {
    int ticks;
    Paired vs_original;
    Paired vs_control;
};

std::vector<ThresholdScore> score_thresholds(const SimData& sd, const PolicyFamily& family,
                                             const std::set<int>& ticks, int start,
                                             int count, int threads) {
    const auto original =
        eval_stream_range(sd, family.original.fleet, family.original.tree, start, count, threads);
    const auto control = eval_stream_range(sd, family.original.fleet, family.control, start, count, threads);
    const std::vector<int> grid(ticks.begin(), ticks.end());
    std::vector<ThresholdScore> scores(grid.size());
    parallel_for(static_cast<int>(grid.size()), threads, [&](int i) {
        auto& score = scores[i];
        score.ticks = grid[i];
        const auto tree = family.with_protection(grid[i]);
        for (int j = 0; j < count; ++j) {
            const auto test = run_sim(sd, start + j, family.original.fleet, tree);
            score.vs_original.add(original[j], test);
            score.vs_control.add(control[j], test);
        }
    });
    std::sort(scores.begin(), scores.end(), [](const ThresholdScore& a, const ThresholdScore& b) {
        return std::tie(a.vs_original.rejected, a.vs_original.mean, a.ticks)
             < std::tie(b.vs_original.rejected, b.vs_original.mean, b.ticks);
    });
    return scores;
}

bool active(const ThresholdScore& row) {
    return row.vs_original.rejected == 0 && row.vs_control.rejected == 0 &&
           row.vs_control.changed_cost > 0;
}

void log_stage(std::ostream& csv, const char* stage, const std::vector<ThresholdScore>& rows) {
    for (const auto& r : rows) {
        csv << stage << ',' << double(r.ticks) / ticks_per_minute << ',' << r.vs_original.n << ','
            << r.vs_original.rejected << ',' << r.vs_original.mean << ',' << r.vs_original.se() << ','
            << r.vs_control.mean << ',' << r.vs_control.se() << ',' << r.vs_control.changed_cost << '\n';
    }
}

int tune(SimData& sd, const PolicyFamily& family, const std::filesystem::path& output, int threads) {
    std::ofstream csv(output / "results" / "thresholds.csv");
    csv << "stage,T_min,paired,rejected,delta_vs_01,se_vs_01,delta_vs_02,se_vs_02,seeds_changed_vs_02\n"
        << std::fixed << std::setprecision(6);
    const int discovery = yb::append_streams(sd, discovery_first, discovery_count);
    std::set<int> coarse{260, 280, 320, 400, 480};
    for (int t = 0; t <= 240; t += 5) coarse.insert(t); // 0 to 12 minutes by 0.25.
    const auto first = score_thresholds(sd, family, coarse, discovery, discovery_count, threads);
    log_stage(csv, "coarse", first);

    std::set<int> fine;
    int centers = 0;
    for (const auto& r : first) {
        if (!active(r)) continue;
        if (++centers > 5) break;
        for (int t = std::max(0, r.ticks - 5); t <= std::min(480, r.ticks + 5); ++t) {
            if (!coarse.contains(t)) fine.insert(t);
        }
    }
    const auto refined = score_thresholds(sd, family, fine, discovery, discovery_count, threads);
    log_stage(csv, "refinement", refined);

    std::vector<ThresholdScore> combined = first;
    combined.insert(combined.end(), refined.begin(), refined.end());
    std::sort(combined.begin(), combined.end(), [](const ThresholdScore& a, const ThresholdScore& b) {
        return std::tie(a.vs_original.rejected, a.vs_original.mean, a.ticks)
             < std::tie(b.vs_original.rejected, b.vs_original.mean, b.ticks);
    });
    std::set<int> shortlist{80, combined.front().ticks}; // Keep the 4-minute illustrated case.
    int shortlisted = 0;
    for (const auto& r : combined) {
        if (!active(r)) continue;
        if (++shortlisted > 12) break;
        shortlist.insert(r.ticks);
    }
    std::cout << "[backlog tuning] coarse=" << first.size() << " refined=" << refined.size()
              << " selection=" << shortlist.size() << std::endl;

    const int selection = yb::append_streams(sd, selection_first, selection_count);
    const auto selected = score_thresholds(sd, family, shortlist, selection, selection_count, threads);
    log_stage(csv, "selection", selected);
    csv.close();
    if (!csv) throw std::runtime_error("failed to write threshold results");
    const auto best = std::find_if(selected.begin(), selected.end(), active);
    if (best == selected.end()) {
        throw std::runtime_error("no eligible threshold changed a score on the selection seeds");
    }
    std::cout << std::fixed << std::setprecision(5)
              << "[backlog locked] T=" << double(best->ticks) / ticks_per_minute
              << " delta_vs_01=" << best->vs_original.mean << " delta_vs_02=" << best->vs_control.mean
              << " changed=" << best->vs_control.changed_cost << '/' << selection_count << std::endl;
    return best->ticks;
}

void report_policy(std::ostream& csv, const char* policy, const std::vector<SimMetrics>& scores) {
    std::vector<double> costs;
    double wait = 0.0, urgent = 0.0;
    int max_q = 0, end_q = 0;
    for (const auto& score : scores) {
        max_q = std::max(max_q, score.max_q);
        end_q = std::max(end_q, score.end_q);
        if (!valid(score)) continue;
        costs.push_back(score.cost);
        wait += score.w_bar;
        urgent += score.u_95;
    }
    const int n = static_cast<int>(costs.size());
    csv << policy << ',' << scores.size() << ',' << scores.size() - n << ',';
    if (!n) {
        csv << "NA,NA,NA,NA," << max_q << ',' << end_q << '\n';
        return;
    }
    std::sort(costs.begin(), costs.end());
    const int tail_n = (n + 19) / 20;
    const double tail = std::accumulate(costs.end() - tail_n, costs.end(), 0.0) / tail_n;
    csv << std::accumulate(costs.begin(), costs.end(), 0.0) / n << ',' << wait / n << ','
        << urgent / n << ',' << tail << ',' << max_q << ',' << end_q << '\n';
}

Paired report_pair(std::ostream& csv, const char* label, const std::vector<SimMetrics>& base,
                   const std::vector<SimMetrics>& test) {
    Paired paired;
    for (std::size_t i = 0; i < base.size(); ++i) paired.add(base[i], test[i]);
    const int n = std::max(1, paired.n);
    csv << label << ',' << paired.n << ',' << paired.rejected << ',' << paired.mean << ','
        << paired.se() << ',' << paired.changed_cost << ',' << paired.wait / n << ','
        << paired.urgent / n << ',' << (paired.improves() ? "improves" : "not_confirmed") << '\n';
    return paired;
}

void audit(SimData& sd, const PolicyFamily& family, int ticks, const std::filesystem::path& output,
           int threads) {
    const auto challenger = family.with_protection(ticks);
    const std::array<Tree, 3> trees{family.original.tree, family.control, challenger};
    std::array<std::vector<SimMetrics>, 3> scores;
    // Reuse batches so the 3,600-scenario audit stays within memory limits.
    sd.streams.clear();
    sd.base_events.clear();
    constexpr int batch = 120;
    for (int done = 0; done < audit_count; done += batch) {
        const int count = std::min(batch, audit_count - done);
        const int start = yb::append_streams(sd, audit_first + done, count);
        for (std::size_t i = 0; i < trees.size(); ++i) {
            const auto sample = eval_stream_range(sd, family.original.fleet, trees[i], start, count, threads);
            scores[i].insert(scores[i].end(), sample.begin(), sample.end());
        }
        sd.streams.clear();
        sd.base_events.clear();
    }

    std::ofstream summary(output / "results" / "summary.csv");
    std::ofstream comparisons(output / "results" / "comparisons.csv");
    std::ofstream raw(output / "results" / "validation_by_seed.csv");
    summary << "policy,seeds,disqualified,mean_cost,mean_wait,urgent_p95,mean_worst_5pct,maxQ,endQmax\n"
            << std::fixed << std::setprecision(6);
    comparisons << "comparison,paired,rejected,delta_cost,standard_error,seeds_changed,delta_wait,delta_urgent_p95,decision\n"
                << std::fixed << std::setprecision(6);
    raw << "seed,policy,cost,mean_wait,urgent_p95,maxQ,endQ,disqualified\n"
        << std::fixed << std::setprecision(8);
    constexpr std::array names{"01_baseline", "02_budget_control", "03_aged_backlog"};
    for (std::size_t i = 0; i < scores.size(); ++i) {
        report_policy(summary, names[i], scores[i]);
        for (int j = 0; j < audit_count; ++j) {
            const auto& r = scores[i][j];
            raw << audit_first + j << ',' << names[i] << ',' << r.cost << ',' << r.w_bar << ','
                << r.u_95 << ',' << r.max_q << ',' << r.end_q << ',' << !valid(r) << '\n';
        }
    }
    report_pair(comparisons, "02_minus_01", scores[0], scores[1]);
    const auto exception = report_pair(comparisons, "03_minus_02", scores[1], scores[2]);
    const auto submission = report_pair(comparisons, "03_minus_01", scores[0], scores[2]);
    summary.close();
    comparisons.close();
    raw.close();
    if (!summary || !comparisons || !raw) throw std::runtime_error("failed to write validation results");
    std::cout << "[backlog audit] 03-02=" << exception.mean << " +/- " << exception.se()
              << " changed=" << exception.changed_cost << '/' << audit_count
              << " | 03-01=" << submission.mean << " +/- " << submission.se()
              << " | " << (exception.improves() && submission.improves() ? "CHALLENGER_WINS" : "KEEP_BASELINE")
              << '\n';
}

int run(SimData& sd, int argc, char** argv, int threads) {
    if (argc != 4) {
        std::cerr << "Usage: --yb-aged-backlog <baseline.csv> <new-results-directory>\n";
        return 2;
    }
    try {
        const auto family = make_family(yb::read_policy(argv[2], sd.fixed_dir));
        const std::filesystem::path output = argv[3];
        if (!std::filesystem::create_directory(output)) {
            throw std::invalid_argument("choose a new results directory");
        }
        std::filesystem::create_directory(output / "trees");
        std::filesystem::create_directory(output / "results");
        const int selected = tune(sd, family, output, threads);
        const int fleet = family.original.fleet;
        const std::array trees{family.original.tree, family.control, family.with_protection(selected)};
        constexpr std::array names{"01_baseline.csv", "02_budget_control.csv", "03_aged_backlog.csv"};
        for (std::size_t i = 0; i < trees.size(); ++i) {
            const auto file = output / "trees" / names[i];
            save_csv_tree(sd, file.string(), fleet, trees[i]);
            if (!std::filesystem::is_regular_file(file) || !std::filesystem::file_size(file)) {
                throw std::runtime_error("failed to write " + file.string());
            }
        }
        audit(sd, family, selected, output, threads);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

} // namespace aged_backlog
