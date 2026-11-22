#pragma once

namespace backlog_demand_profiles {

constexpr int first_seed = 43001;
constexpr int seed_count = 1200;
constexpr std::array block_start{480, 840, 1200, 1560, 1920, 2280};
constexpr double block_minutes = 90.0;
constexpr double observation_minutes = 60.0;
constexpr double late_minutes = 8.0;
constexpr int fixed_t = 80; // T = 4 minutes, set before the profile comparison.

struct Profile {
    const char* name;
    double surge_minutes;
    double surge_rate;
    bool affect_pack;
};

constexpr std::array profiles{
    Profile{"normal", 0, 1, false},
    Profile{"short_recv", 10, 3, false},
    Profile{"sustained_recv", 40, 1.5, false},
    Profile{"sustained_recv_pack", 40, 1.5, true}
};

struct Focus {
    int recv;
    int pack;

    bool includes(const Arrival& ar, const Profile& p) const {
        return !ar.u && (ar.o == recv || (p.affect_pack && ar.o == pack));
    }
};

int department(const SimData& sd, const std::string& name) {
    for (std::size_t i = 0; i < sd.depts.size(); ++i) {
        if (sd.depts[i] == name) return sd.pd_nodes[i];
    }
    throw std::invalid_argument("case has no department " + name);
}

double offset_in_block(double time) {
    const auto block = std::upper_bound(block_start.begin(), block_start.end(), time);
    return block == block_start.begin() ? block_minutes : time - *(block - 1);
}

struct InputCounts {
    int first10 = 0;
    int first40 = 0;
    int full90 = 0;
};

InputCounts apply_profile(std::vector<Arrival>& arrivals, const Focus& focus, const Profile& p) {
    InputCounts counts;
    const double mass = p.surge_minutes * p.surge_rate;
    const double recovery_rate = (block_minutes - mass) / (block_minutes - p.surge_minutes);
    for (auto& ar : arrivals) {
        if (!focus.includes(ar, p)) continue;
        const double offset = offset_in_block(ar.at);
        if (offset >= block_minutes) continue;
        const double adjusted = offset <= mass ? offset / p.surge_rate
                             : p.surge_minutes + (offset - mass) / recovery_rate;
        ar.at += adjusted - offset;
        if (offset_in_block(ar.at) >= block_minutes) {
            throw std::runtime_error("retimed arrival left its original block");
        }
        counts.first10 += adjusted < 10.0;
        counts.first40 += adjusted < 40.0;
        ++counts.full90;
    }
    std::stable_sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) {
        return a.at < b.at;
    });
    return counts;
}

struct Arm {
    const char* name;
    backlog_keep_urgent::Variant policy;
};

std::array<Arm, 6> arms_for(const backlog_keep_urgent::Family& family) {
    Tree stricter_control = family.without_far_leaf();
    Tree stricter_legal = family.legal(fixed_t);
    stricter_control.nodes[stricter_control.nodes[1].lo].th = 2.0;
    stricter_legal.nodes[stricter_legal.nodes[1].lo].th = 2.0;
    if (!is_valid_tree(stricter_control) || !is_valid_tree(stricter_legal)) {
        throw std::invalid_argument("invalid three-request comparison");
    }
    return {{
        {"01_baseline", {family.reference.tree, 0}},
        {"02_virtual_age4", backlog_keep_urgent::virtual_rule(family, fixed_t)},
        {"03_q2_control", {family.without_far_leaf(), 0}},
        {"04_q2_age4", {family.legal(fixed_t), fixed_t}},
        {"05_q3_control", {stricter_control, 0}},
        {"06_q3_age4", {stricter_legal, fixed_t}}
    }};
}

struct Metric {
    SimMetrics sim;
    int general = 0, late = 0;
    int focal_general = 0, focal_late = 0;

    double late_rate() const { return 1000.0 * late / general; }
    double focal_rate() const { return 1000.0 * focal_late / focal_general; }
};

Metric evaluate(const SimData& sd, int stream, int fleet, const Arm& arm,
                const Focus& focus, const Profile& profile) {
    SimTrace trace;
    Metric score{backlog_keep_urgent::evaluate(sd, stream, fleet, arm.policy, false, &trace)};
    if (!aged_backlog::valid(score.sim)) return score;

    const auto& arrivals = sd.streams[stream];
    for (std::size_t i = 0; i < arrivals.size(); ++i) {
        const auto& ar = arrivals[i];
        if (ar.u || ar.at < sd.warm || ar.at > sd.t_end - late_minutes) continue;
        const bool late = trace.pickup[i] < 0 || trace.pickup[i] - ar.at > late_minutes;
        ++score.general;
        score.late += late;
        if (focus.includes(ar, profile) && offset_in_block(ar.at) < observation_minutes) {
            ++score.focal_general;
            score.focal_late += late;
        }
    }
    return score;
}

struct AverageDiff {
    int n = 0;
    double mean = 0, m2 = 0;

    void add(double d) {
        ++n;
        const double residual = d - mean;
        mean += residual / n;
        m2 += residual * (d - mean);
    }

    double se() const { return n > 1 ? std::sqrt(m2 / (n - 1) / n) : 0; }
};

using Results = std::array<std::vector<Metric>, 6>;

Results run_profile(SimData& sd, const Focus& focus, const Profile& profile,
                    const std::array<Arm, 6>& arms, int fleet, int threads, std::ofstream& inputs) {
    Results data;
    InputCounts total;
    constexpr int batch_size = 120;
    for (int done = 0; done < seed_count; done += batch_size) {
        sd.streams.clear();
        sd.base_events.clear();
        const int count = std::min(batch_size, seed_count - done);
        std::vector<InputCounts> batch_counts(count);
        sd.streams.resize(count);
        sd.base_events.resize(count);
        parallel_for(count, threads, [&](int i) {
            auto arrivals = generate_arrivals(sd, first_seed + done + i);
            batch_counts[i] = apply_profile(arrivals, focus, profile);
            auto& events = sd.base_events[i];
            events.reserve(arrivals.size());
            for (int id = 0; id < static_cast<int>(arrivals.size()); ++id) {
                events.push_back({round9(arrivals[id].at), 0, id});
            }
            sd.streams[i] = std::move(arrivals);
        });
        for (const auto& input : batch_counts) {
            total.first10 += input.first10;
            total.first40 += input.first40;
            total.full90 += input.full90;
        }
        if (done == 0) {
            for (int i = 0; i < 3; ++i) {
                for (const auto& arm : arms) {
                    const auto fast = backlog_keep_urgent::evaluate(sd, i, fleet, arm.policy);
                    const auto brute = backlog_keep_urgent::evaluate(sd, i, fleet, arm.policy, true);
                    if (std::abs(fast.cost - brute.cost) > 1e-9) {
                        throw std::runtime_error("profile dispatch parity failed");
                    }
                }
            }
        }
        for (std::size_t a = 0; a < arms.size(); ++a) {
            data[a].resize(done + count);
            parallel_for(count, threads, [&](int i) {
                data[a][done + i] = evaluate(sd, i, fleet, arms[a], focus, profile);
            });
        }
    }
    inputs << profile.name << ',' << seed_count << ',' << total.first10 << ','
           << total.first40 << ',' << total.full90 << '\n';
    return data;
}

void summaries(const Profile& profile, const std::array<Arm, 6>& arms, const Results& rows,
               std::ofstream& summary, std::ofstream& comparisons, std::ofstream& raw) {
    for (std::size_t a = 0; a < arms.size(); ++a) {
        std::vector<double> costs;
        double urgent = 0, general = 0, focal = 0;
        int fq_count = 0, max_q = 0;
        for (int i = 0; i < seed_count; ++i) {
            const auto& r = rows[a][i];
            raw << profile.name << ',' << first_seed + i << ',' << arms[a].name << ','
                << r.sim.cost << ',' << r.sim.u_95 << ',' << r.general << ',' << r.late << ','
                << r.focal_general << ',' << r.focal_late << ',' << r.sim.max_q << ','
                << !aged_backlog::valid(r.sim) << '\n';
            max_q = std::max(max_q, r.sim.max_q);
            if (!aged_backlog::valid(r.sim)) continue;
            costs.push_back(r.sim.cost);
            urgent += r.sim.u_95;
            general += r.late_rate();
            if (r.focal_general) {
                focal += r.focal_rate();
                ++fq_count;
            }
        }
        std::sort(costs.begin(), costs.end());
        const int n = static_cast<int>(costs.size()), tail = (n + 19) / 20;
        summary << profile.name << ',' << arms[a].name << ',' << seed_count << ',' << seed_count - n << ',';
        if (!n) summary << "NA,NA,NA,NA,NA,";
        else {
            summary << std::accumulate(costs.begin(), costs.end(), 0.0) / n << ','
                    << urgent / n << ',' << general / n << ',';
            if (fq_count) summary << focal / fq_count << ',';
            else summary << "NA,";
            summary << std::accumulate(costs.end() - tail, costs.end(), 0.0) / tail << ',';
        }
        summary << max_q << '\n';
    }

    for (const auto& [test, base] : std::array<std::pair<int,int>, 7>{
             {{1,0},{2,0},{3,2},{3,0},{4,2},{5,4},{5,0}}}) {
        aged_backlog::Paired cost;
        AverageDiff global_late, focal_late;
        for (int i = 0; i < seed_count; ++i) {
            const auto& a = rows[base][i];
            const auto& b = rows[test][i];
            cost.add(a.sim, b.sim);
            if (!aged_backlog::valid(a.sim) || !aged_backlog::valid(b.sim)) continue;
            global_late.add(b.late_rate() - a.late_rate());
            if (a.focal_general && b.focal_general) focal_late.add(b.focal_rate() - a.focal_rate());
        }
        comparisons << profile.name << ',' << arms[test].name << "_minus_" << arms[base].name << ','
                    << cost.n << ',' << cost.rejected << ',' << cost.mean << ',' << cost.se() << ','
                    << cost.urgent / std::max(1, cost.n) << ',' << global_late.mean << ',' << global_late.se()
                    << ',' << focal_late.mean << ',' << focal_late.se() << ',' << focal_late.n << '\n';
    }
}

int run(SimData& sd, int argc, char** argv, int threads) {
    if (argc != 4) {
        std::cerr << "Usage: --yb-backlog-demand-profiles <baseline.csv> <new-results-directory>\n";
        return 2;
    }
    try {
        if (block_start.front() < sd.warm || block_start.back() + block_minutes > sd.t_end - late_minutes) {
            throw std::invalid_argument("invalid profile windows for this case");
        }
        const auto family = backlog_keep_urgent::make_family(yb::read_policy(argv[2], sd.fixed_dir));
        const auto arms = arms_for(family);
        const Focus focus{department(sd, "RECV"), department(sd, "PACK")};
        const std::filesystem::path out = argv[3];
        if (!std::filesystem::create_directory(out)) throw std::invalid_argument("choose a new results directory");
        std::filesystem::create_directory(out / "trees");
        std::filesystem::create_directory(out / "results");
        for (const auto& arm : arms) {
            if (arm.policy.virtual_parent) continue;
            const auto file = out / "trees" / (std::string(arm.name) + ".csv");
            save_csv_tree(sd, file.string(), family.reference.fleet, arm.policy.tree);
            if (!std::filesystem::is_regular_file(file) || !std::filesystem::file_size(file)) {
                throw std::runtime_error("failed to write legal tree");
            }
        }
        std::ofstream inputs(out / "results" / "input_profiles.csv");
        std::ofstream summary(out / "results" / "summary.csv");
        std::ofstream pairs(out / "results" / "comparisons.csv");
        std::ofstream raw(out / "results" / "validation_by_seed.csv");
        inputs << "profile,seeds,target_arrivals_first10,target_arrivals_first40,target_arrivals_full90\n";
        summary << "profile,policy,seeds,rejected,mean_cost,urgent_p95,late_general_per_1000,late_focal_per_1000,worst_5pct_mean,maxQ\n"
                << std::fixed << std::setprecision(8);
        pairs << "profile,comparison,paired,rejected,delta_cost,se_cost,delta_urgent_p95,delta_late_general_per_1000,"
                 "se_late_general,delta_late_focal_per_1000,se_late_focal,focal_paired\n"
              << std::fixed << std::setprecision(8);
        raw << "profile,seed,policy,cost,urgent_p95,general_count,general_late,focal_count,focal_late,maxQ,rejected\n"
            << std::fixed << std::setprecision(8);
        for (const auto& profile : profiles) {
            const auto results = run_profile(sd, focus, profile, arms, family.reference.fleet, threads, inputs);
            summaries(profile, arms, results, summary, pairs, raw);
            inputs.flush();
            summary.flush();
            pairs.flush();
            std::cout << "[demand profiles] completed " << profile.name << " (" << seed_count << " seeds)" << std::endl;
        }
        inputs.close();
        summary.close();
        pairs.close();
        raw.close();
        if (!inputs || !summary || !pairs || !raw) throw std::runtime_error("failed to write profile results");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}

} // namespace backlog_demand_profiles
