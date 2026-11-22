#pragma once

// Included after agv_yb_search.hpp to share stream generation and ranking.
namespace sort_supply {

constexpr double max_de = 65.0;
constexpr int samples_per_group = 6;
constexpr int group_count = 4;

struct Opportunity {
    int dispatch_index;
    double time;
    int vehicle;
    int original;
    int inbound;
    double original_de;
    double inbound_de;
    int other_inbound;
    int waiting_urgent;
    int group;
};

struct Trial {
    Opportunity opportunity;
    bool replay_ok;
    bool eligible_metrics;
    double sort_free_base, sort_free_test;
    double urgent_arrival;
    double urgent_pickup_base, urgent_pickup_test;
    double original_pickup_base, original_pickup_test;
    int cohort_size;
    double cohort_pickup_delta;
    bool next_urgent_uses_inbound;
    double cost_base, cost_test;
};

struct SeedResult {
    std::array<int, group_count> eligible{};
    std::vector<Trial> trials;
};

int pack_flow(const SimData& sd) {
    for (int f = 0; f < sd.n_flows; ++f) {
        const auto& flow = sd.flows[f];
        if (sd.depts[flow.o_dept] == "PACK" && sd.depts[flow.d_dept] == "SORT") return f;
    }
    throw std::invalid_argument("case has no PACK to SORT flow");
}

bool has_idle_at(const DispatchView& view, int sort_node) {
    for (uint32_t idle = view.idle_mask; idle; idle &= idle - 1) {
        if (view.positions[std::countr_zero(idle)] == sort_node) return true;
    }
    return false;
}

SeedResult replay_seed(const SimData& sd, const yb::Policy& policy,
                       int stream, int seed, int supply_flow, int sort_node) {
    const auto& arrivals = sd.streams[stream];
    SeedResult result;
    std::array<std::vector<Opportunity>, group_count> samples;
    std::mt19937 rng(seed);
    SimTrace base;
    base.choose_request = [&](const DispatchView& view) {
        const auto& original = arrivals[view.request];
        const auto& supply_queue = view.queues[supply_flow];
        const int head = view.head[supply_flow];
        if (view.time < sd.warm || view.time > sd.t_end - 20.0 || original.u ||
            original.d == sort_node || head >= static_cast<int>(supply_queue.size()) ||
            has_idle_at(view, sort_node)) return view.request;
        const int supply_rid = supply_queue[head];
        const auto& sp = sd.sp_mat[view.positions[view.vehicle]];
        const double distance = sp[arrivals[supply_rid].o];
        if (distance > max_de) return view.request;
        int inbound = 0;
        for (int a = 0; a < policy.fleet; ++a) {
            const auto& vehicle = view.vehicles[a];
            if (vehicle.status != 0 && vehicle.rid >= 0 && arrivals[vehicle.rid].d == sort_node) ++inbound;
        }
        const int group = 2 * (inbound > 0) + (distance > 25.0);
        const int count = ++result.eligible[group];
        const Opportunity candidate{view.index, view.time, view.vehicle, view.request, supply_rid,
                                    sp[original.o], distance, inbound, view.waiting_at[sort_node], group};
        auto& bucket = samples[group];
        if (count <= samples_per_group) bucket.push_back(candidate);
        else {
            const int replacement = yb::pick(rng, count);
            if (replacement < samples_per_group) bucket[replacement] = candidate;
        }
        return view.request;
    };

    const auto base_score = run_sim(sd, stream, policy.fleet, policy.tree, -1, false, &base);
    for (auto& bucket : samples) {
        std::sort(bucket.begin(), bucket.end(), [](const Opportunity& a, const Opportunity& b) {
            return a.dispatch_index < b.dispatch_index;
        });
        for (const auto& o : bucket) {
            const auto after = std::upper_bound(arrivals.begin(), arrivals.end(), o.time,
                [](double t, const Arrival& request) { return t < request.at; });
            const auto next = std::find_if(after, arrivals.end(), [](const Arrival& a) { return a.u == 1; });
            const int urgent = static_cast<int>(next - arrivals.begin());

            SimTrace test;
            int overrides = 0;
            test.choose_request = [&](const DispatchView& view) {
                if (view.index != o.dispatch_index) return view.request;
                const auto& queue = view.queues[supply_flow];
                const int head = view.head[supply_flow];
                if (view.time == o.time && view.request == o.original && view.vehicle == o.vehicle &&
                    head < static_cast<int>(queue.size()) && queue[head] == o.inbound) {
                    ++overrides;
                    return o.inbound;
                }
                return view.request;
            };
            const auto test_score = run_sim(sd, stream, policy.fleet, policy.tree, -1, false, &test);
            const auto get = [](const std::vector<double>& x, int i) {
                return i < static_cast<int>(x.size()) ? x[i] : -1.0;
            };
            const double base_sort = get(base.unload, o.inbound), test_sort = get(test.unload, o.inbound);
            const double base_pickup = get(base.pickup, urgent), test_pickup = get(test.pickup, urgent);
            const double base_orig = get(base.pickup, o.original), test_orig = get(test.pickup, o.original);
            const bool valid = overrides == 1 && base_sort >= 0 && test_sort >= 0 &&
                               base_pickup >= 0 && test_pickup >= 0 &&
                               base_orig >= 0 && test_orig >= 0 &&
                               base_score.cost < yb::disqualified_cost && test_score.cost < yb::disqualified_cost;
            const bool direct = valid && test.previous_delivery[urgent] == o.inbound;
            int cohort_size = 0;
            double cohort_delta = 0.0;
            for (auto request = next; request != arrivals.end() && request->at <= o.time + 10.0; ++request) {
                if (!request->u) continue;
                const int id = static_cast<int>(request - arrivals.begin());
                const double cutoff = request->at + 10.0;
                const double original_pick = get(base.pickup, id);
                const double override_pick = get(test.pickup, id);
                cohort_delta += (override_pick < 0 ? cutoff : std::min(cutoff, override_pick))
                              - (original_pick < 0 ? cutoff : std::min(cutoff, original_pick));
                ++cohort_size;
            }
            const double urgent_arrival = next != arrivals.end() ? next->at : -1.0;
            result.trials.push_back({o, overrides == 1, valid, base_sort, test_sort, urgent_arrival,
                                     base_pickup, test_pickup, base_orig, test_orig, cohort_size, cohort_delta,
                                     direct, base_score.cost, test_score.cost});
        }
    }
    return result;
}

int replay(SimData& sd, const yb::Policy& baseline, const std::string& output,
           int first, int count, int threads) {
    const int flow = pack_flow(sd);
    const int sort_node = sd.pd_nodes[sd.flows[flow].d_dept];
    const int start = yb::append_streams(sd, first, count);
    std::vector<SeedResult> results(count);
    parallel_for(count, threads, [&](int i) {
        results[i] = replay_seed(sd, baseline, start + i, first + i, flow, sort_node);
    });

    std::ofstream csv(output);
    csv << "seed,group,dispatch_index,time,other_inbound,waiting_urgent,original_de,inbound_de,"
           "replay_ok,complete,sort_free_base,sort_free_test,urgent_arrival,urgent_pickup_base,"
           "urgent_pickup_test,original_pickup_base,original_pickup_test,cohort_size,cohort_pickup_delta,"
           "next_urgent_uses_inbound,cost_base,cost_test\n";
    csv << std::fixed << std::setprecision(6);
    std::array<int, group_count> eligible{}, sampled{}, complete{};
    for (int i = 0; i < count; ++i) {
        for (int g = 0; g < group_count; ++g) eligible[g] += results[i].eligible[g];
        for (const auto& t : results[i].trials) {
            const auto& o = t.opportunity;
            ++sampled[o.group];
            complete[o.group] += t.eligible_metrics;
            csv << first + i << ',' << o.group << ',' << o.dispatch_index << ',' << o.time << ','
                << o.other_inbound << ',' << o.waiting_urgent << ',' << o.original_de << ','
                << o.inbound_de << ',' << t.replay_ok << ',' << t.eligible_metrics << ','
                << t.sort_free_base << ',' << t.sort_free_test << ',' << t.urgent_arrival << ','
                << t.urgent_pickup_base << ',' << t.urgent_pickup_test << ','
                << t.original_pickup_base << ',' << t.original_pickup_test << ','
                << t.cohort_size << ',' << t.cohort_pickup_delta << ','
                << t.next_urgent_uses_inbound << ',' << t.cost_base << ',' << t.cost_test << '\n';
        }
    }
    csv.close();
    if (!csv) throw std::runtime_error("could not write " + output);
    for (int g = 0; g < group_count; ++g) {
        std::cout << "[sort replay] group=" << g << " eligible=" << eligible[g]
                  << " sampled=" << sampled[g] << " complete=" << complete[g] << '\n';
    }
    return 0;
}

struct Slot {
    int parent;
    bool high;
};

void near_slots(const Tree& tree, int nid, Slot slot, std::vector<Slot>& out) {
    if (!nid) {
        out.push_back(slot);
        return;
    }
    const auto& node = tree.nodes[nid];
    if (node.feat == 3) return; // An ancestor already uses the loaded-distance feature.
    near_slots(tree, node.lo, {nid, false}, out);
    // On the urgent-only path, fitted loaded-distance ranges would be constant.
    if (node.feat != 0) near_slots(tree, node.hi, {nid, true}, out);
}

void bind(Tree& tree, Slot slot, int child) {
    auto& parent = tree.nodes[slot.parent];
    (slot.high ? parent.hi : parent.lo) = child;
}

void detach(Tree& tree, int leaf) {
    for (int i = 1; i <= tree.n_nodes; ++i) {
        if (tree.nodes[i].lo == leaf) tree.nodes[i].lo = 0;
        if (tree.nodes[i].hi == leaf) tree.nodes[i].hi = 0;
    }
}

std::vector<yb::Policy> legal_policies(const yb::Policy& baseline, const std::vector<double>& thresholds) {
    const auto& original = baseline.tree;
    if (original.nodes[1].feat != 2 || !original.nodes[1].is_min) {
        throw std::invalid_argument("SORT search requires a de,min root");
    }
    std::vector<int> budget;
    for (int i = 2; i <= original.n_nodes; ++i) {
        const auto& node = original.nodes[i];
        if (!node.lo && !node.hi &&
            ((node.feat == 0 && !node.is_min) || (node.feat == 1 && node.is_min))) budget.push_back(i);
    }

    std::vector<yb::Policy> proposals;
    std::set<std::string> seen{tree_key_str(original)};
    for (int leaf : budget) {
        Tree freed = original;
        detach(freed, leaf);
        std::vector<Slot> slots;
        near_slots(freed, freed.nodes[1].lo, {1, false}, slots);
        for (const Slot slot : slots) {
            for (double de : {25.0, 50.0, 65.0}) {
                for (double qo : {1.0, 2.0, 3.0}) {
                    for (double dl : thresholds) {
                        Tree tree = freed;
                        tree.nodes[1].th = de;
                        const int near = tree.nodes[1].lo;
                        if (near && tree.nodes[near].feat == 4) tree.nodes[near].th = qo;
                        bind(tree, slot, leaf);
                        tree.nodes[leaf] = {3, false, dl, 0, 0};
                        if (is_valid_tree(tree) && seen.insert(tree_key_str(tree)).second) {
                            proposals.push_back({baseline.fleet, tree});
                        }
                    }
                }
            }
        }
    }
    return proposals;
}

int search(SimData& sd, const yb::Policy& baseline, const std::string& output, int threads) {
    const int train = yb::append_streams(sd, 16001, 64);
    // Keep PACK-inclusive candidates separate from the SHIP->RECV-only negative control.
    const auto promotion = legal_policies(baseline, {50.0, 65.0});
    const auto control = legal_policies(baseline, {90.0});
    auto shortlist = [&](const std::vector<yb::Policy>& candidates) {
        const auto pilot = yb::rank(sd, candidates, train, 16, threads);
        const auto trained = yb::rank(sd, yb::top(pilot, 32), train, 64, threads);
        return yb::top(trained, 12);
    };
    const auto finalists = shortlist(promotion);
    const auto control_finalists = shortlist(control);
    const int selection = yb::append_streams(sd, 17001, 240);
    const auto confirmed = yb::rank(sd, finalists, selection, 240, threads);
    const auto confirmed_control = yb::rank(sd, control_finalists, selection, 240, threads);
    if (confirmed.empty()) throw std::runtime_error("no eligible SORT candidate on selection seeds");
    const auto& best = confirmed.front();
    const double reference = eval_streams(sd, baseline.fleet, baseline.tree, selection, selection + 240);
    save_csv_tree(sd, output, best.policy.fleet, best.policy.tree);
    if (!std::filesystem::is_regular_file(output) || std::filesystem::file_size(output) == 0) {
        throw std::runtime_error("could not write " + output);
    }
    std::cout << std::fixed << std::setprecision(5)
              << "[sort search] valid_unique=" << promotion.size() << " finalists=" << finalists.size()
              << " reference=" << reference << " challenger=" << best.cost
              << " delta=" << best.cost - reference << '\n';
    if (!confirmed_control.empty()) {
        std::cout << "[sort negative control] valid_unique=" << control.size()
                  << " finalists=" << control_finalists.size()
                  << " challenger=" << confirmed_control.front().cost
                  << " delta=" << confirmed_control.front().cost - reference << '\n';
    }
    return 0;
}

int parse_int(const std::string& value, int low, int high) {
    std::size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size() || result < low || result > high) {
        throw std::invalid_argument("value outside allowed range: " + value);
    }
    return result;
}

int run(SimData& sd, int argc, char** argv, int threads) {
    const bool is_replay = std::string(argv[1]) == "--yb-sort-replay";
    if (argc != (is_replay ? 6 : 4)) {
        std::cerr << "Usage: --yb-sort-replay <baseline.csv> <out.csv> <first_seed> <count>"
                     " | --yb-sort-search <baseline.csv> <out.csv>\n";
        return 2;
    }
    try {
        const auto baseline = yb::read_policy(argv[2], sd.fixed_dir);
        if (std::filesystem::exists(argv[3])) throw std::invalid_argument("choose a new output path");
        if (is_replay) {
            const int seed = parse_int(argv[4], 1, 2000000000);
            const int count = parse_int(argv[5], 1, 256);
            return replay(sd, baseline, argv[3], seed, count, threads);
        }
        return search(sd, baseline, argv[3], threads);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}

} // namespace sort_supply
